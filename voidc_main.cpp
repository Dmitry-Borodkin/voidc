//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"
#include "voidc_util.h"
#include "voidc_visitor.h"
#include "vpeg_context.h"
#include "vpeg_voidc.h"

#include <iostream>
#include <fstream>
#include <set>
#include <list>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include <unistd.h>

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
//- Some utility...
//---------------------------------------------------------------------
static vpeg::grammar_t voidc_grammar;

static
std::any parse_unit(vpeg::context_t &pctx)
{
    auto ret = pctx.grammar.parse("unit", pctx);

    pctx.memo.clear();

    return ret;
}


//--------------------------------------------------------------------
namespace fs = std::filesystem;


//--------------------------------------------------------------------
static std::list<fs::path> import_paths;

#ifdef _WIN32
#define PATHSEP ';'
#else
#define PATHSEP ':'
#endif

void import_paths_initialize(const char *exe_name)
{
    if (auto paths = std::getenv("VOIDC_IMPORT"))
    {
        while(paths)
        {
            fs::path path;

            if (auto p = std::strchr(paths, PATHSEP))
            {
                path = std::string(paths, p);

                paths = p + 1;
            }
            else
            {
                path = std::string(paths);

                paths = nullptr;
            }

            import_paths.push_back(path);
        }
    }
    else
    {
        import_paths = {"."};
    }

    if (exe_name)
    {
        fs::path p(exe_name);

        if (fs::exists(p))
        {
            p = fs::canonical(p);
            p = p.parent_path();

            import_paths.push_back(p);
        }
    }
}

#undef PATHSEP

//--------------------------------------------------------------------
static
fs::path find_file_for_import(const fs::path &parent, const fs::path &filename)
{
    if (filename.is_relative())
    {
        auto p = parent / filename;

        if (fs::exists(p))  return  fs::canonical(p);

        for (auto it : import_paths)
        {
            auto p = it / filename;

            if (fs::exists(p))  return  fs::canonical(p);
        }
    }
    else if (fs::exists(filename))
    {
        return  fs::canonical(filename);
    }

    return  "";
}


//--------------------------------------------------------------------
static std::set<fs::path> already_imported;

//--------------------------------------------------------------------
extern "C"
{

static
void v_import(const char *name)
{
    assert(voidc_global_ctx_t::target == voidc_global_ctx_t::voidc);

    auto &gctx = *voidc_global_ctx_t::voidc;

    auto *parent_cctx = gctx.current_ctx;

    fs::path src_filename = name;

    fs::path parent_path = fs::path(parent_cctx->filename).parent_path();

    src_filename = find_file_for_import(parent_path, src_filename);

    if (!fs::exists(src_filename))
    {
        throw std::runtime_error("Source file not found: " + src_filename.string());
    }

    if (already_imported.count(src_filename))   return;

    already_imported.insert(src_filename);

    fs::path bin_filename = src_filename;

    bin_filename += "c";

    bool use_binary = true;

    if (!fs::exists(bin_filename))
    {
        use_binary = false;
    }
    else
    {
        auto st = fs::last_write_time(src_filename);
        auto bt = fs::last_write_time(bin_filename);

        if (st > bt)  use_binary = false;
    }

    static const char magic[8] = ".voidc\n";

    std::ifstream infs;

    voidc_local_ctx_t lctx(src_filename.string(), gctx);

    if (use_binary)
    {
        infs.open(bin_filename, std::ios::binary);

        size_t buf_len = sizeof(magic);
        auto buf = std::make_unique<char[]>(buf_len);

        std::memset(buf.get(), 0, buf_len);

        infs.read(buf.get(), buf_len);

        if (std::strcmp(magic, buf.get()) == 0)
        {
            auto parent_vpeg_ctx = vpeg::context_t::current_ctx;

            vpeg::context_t::current_ctx = nullptr;

            while(!infs.eof())
            {
                size_t len;

                infs.read((char *)&len, sizeof(len));

                if (infs.eof()) break;      //- WTF ?!?!?

                if (buf_len < len)
                {
                    buf = std::make_unique<char[]>(len);

                    buf_len = len;
                }

                infs.read(buf.get(), len);

                lctx.unit_buffer = LLVMCreateMemoryBufferWithMemoryRange(buf.get(), len, "unit_buffer", false);

                lctx.run_unit_action();
            }

            vpeg::context_t::current_ctx = parent_vpeg_ctx;
        }
        else
        {
            infs.close();

            use_binary = false;
        }
    }

    if (!use_binary)
    {
        infs.open(src_filename, std::ios::binary);

        std::ofstream  outfs;

        outfs.open(bin_filename, std::ios::binary|std::ios::trunc);

        {   char buf[sizeof(magic)];

            std::memset(buf, 0, sizeof(magic));

            outfs.write(buf, sizeof(magic));
        }

        auto compile_visitor = make_compile_visitor();

        {   vpeg::context_t pctx(infs, voidc_grammar);

            auto parent_vpeg_ctx = vpeg::context_t::current_ctx;

            vpeg::context_t::current_ctx = &pctx;

            for(;;)
            {
                auto v = parse_unit(pctx);

                if (!pctx.is_ok())  break;

                if (auto unit = std::any_cast<ast_unit_ptr_t>(v))
                {
                    unit->accept(compile_visitor);

                    unit.reset();

                    if (lctx.unit_buffer)
                    {
                        size_t len = LLVMGetBufferSize(lctx.unit_buffer);

                        outfs.write((char *)&len, sizeof(len));

                        outfs.write(LLVMGetBufferStart(lctx.unit_buffer), len);

                        lctx.run_unit_action();
                    }
                }
                else
                {
                    throw std::runtime_error("Unit parse error!");
                }
            }

            vpeg::context_t::current_ctx = parent_vpeg_ctx;
        }

        outfs.seekp(0);

        outfs.write(magic, sizeof(magic));

        outfs.close();
    }

    infs.close();
}

}   //- extern "C"


//--------------------------------------------------------------------
//- As is...
//--------------------------------------------------------------------
int main(int argc, char *argv[])
{
    {   const char *exe_name = nullptr;

        if (argc > 0) exe_name = argv[0];

        import_paths_initialize(exe_name);
    }

    std::list<std::string> sources;

    while (optind < argc)
    {
        char c;

        if ((c = getopt(argc, argv, "-I:")) != -1)
        {
            //- Option argument

            switch (c)
            {
            case 'I':
                import_paths.push_back(optarg);
                break;

            case 1:
                sources.push_back(optarg);
                break;

            default:
                break;
            }
        }
        else
        {
            break;
        }
    }

    if (sources.empty())  sources.push_back("-");

    voidc_global_ctx_t::static_initialize();

    auto &gctx = *voidc_global_ctx_t::voidc;

    utility::static_initialize();

    {   auto char_ptr_type = LLVMPointerType(gctx.char_type, 0);

        gctx.add_symbol("v_import",
                        LLVMFunctionType(gctx.void_type, &char_ptr_type, 1, false),
                        (void *)v_import
                       );
    }

    voidc_visitor_t::static_initialize();
    v_ast_static_initialize();
    vpeg::grammar_t::static_initialize();
    vpeg::context_t::static_initialize();

    voidc_grammar = make_voidc_grammar();

    vpeg::grammar_t current_grammar = voidc_grammar;

    for (auto &src : sources)
    {
        std::string src_name = src;

        std::istream *istr = nullptr;

        if (src == "-")
        {
            src_name = "<stdin>";

            istr = &std::cin;
        }
        else
        {
            if (!fs::exists(src_name))
            {
                throw std::runtime_error("Source file not found: " + src_name);
            }

            auto infs = new std::ifstream;

            infs->open(src_name, std::ios::binary);

            istr = infs;
        }

        voidc_local_ctx_t lctx(src_name, gctx);

        auto compile_visitor = make_compile_visitor();

        vpeg::context_t pctx(*istr, current_grammar);

        vpeg::context_t::current_ctx = &pctx;

        for(;;)
        {
            auto v = parse_unit(pctx);

            if (!pctx.is_ok())  break;

            if (auto unit = std::any_cast<ast_unit_ptr_t>(v))
            {
                unit->accept(compile_visitor);

                unit.reset();

                lctx.run_unit_action();
            }
            else
            {
                throw std::runtime_error("Unit parse error!");
            }
        }

        vpeg::context_t::current_ctx = nullptr;     //- ?

        current_grammar = pctx.grammar;

        if (src != "-")   delete istr;
    }

#if 0

    for (auto &it : compile_ctx_t::symbol_types)
    {
        printf("%s : ", it.first.c_str());

        fflush(stdout);     //- WTF ?

        if (it.second == compile_ctx_t::LLVMOpaqueType_type)
        {
            auto tv = (LLVMTypeRef)LLVMSearchForAddressOfSymbol(it.first.c_str());

            auto s = LLVMPrintTypeToString(tv);

            printf("TYPE = %s\n", s);

            LLVMDisposeMessage(s);
        }
        else
        {
            auto s = LLVMPrintTypeToString(it.second);

            printf("%s\n", s);

            LLVMDisposeMessage(s);
        }
    }

#endif

    vpeg::context_t::static_terminate();
    vpeg::grammar_t::static_terminate();
    v_ast_static_terminate();
    voidc_visitor_t::static_terminate();

    utility::static_terminate();
    voidc_global_ctx_t::static_terminate();

    return 0;
}


