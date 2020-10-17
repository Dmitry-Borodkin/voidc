//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_target.h"
#include "voidc_util.h"
#include "voidc_visitor.h"
#include "voidc_compiler.h"
#include "vpeg_context.h"
#include "vpeg_voidc.h"

#include <list>
#include <memory>
#include <cstdio>
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

static ast_unit_ptr_t
parse_unit(vpeg::context_t &pctx)
{
    static const auto unit_q = v_quark_from_string("unit");

    auto ret = pctx.grammar.parse(unit_q, pctx);

    pctx.memo.clear();

    if (auto unit = std::any_cast<ast_unit_ptr_t>(&ret))  return *unit;

    size_t line, column;

    pctx.get_line_column(pctx.get_buffer_size(), line, column);

    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    std::string msg = "Parse error in " + lctx.filename + ":"
                      + std::to_string(line+1) + ":"
                      + std::to_string(column);

    throw  std::runtime_error(msg);
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

void
import_paths_initialize(const char *exe_name)
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
static fs::path
find_file_for_import(const fs::path &parent, const fs::path &filename)
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
//- Intrinsics (functions)
//--------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

//--------------------------------------------------------------------
void
v_import(const char *name)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    auto *parent_lctx = vctx.local_ctx;

    fs::path src_filename = name;

    fs::path parent_path = fs::path(parent_lctx->filename).parent_path();

    src_filename = find_file_for_import(parent_path, src_filename);

    if (!fs::exists(src_filename))
    {
        throw std::runtime_error("Import file not found: " + src_filename.string());
    }

    auto src_filename_str = src_filename.string();

    {   auto &tctx = *voidc_global_ctx_t::target;

        if (tctx.imported.count(src_filename_str)) return;

        tctx.imported.insert(src_filename_str);
    }

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

    std::FILE *infs;

    voidc_local_ctx_t lctx(src_filename_str, vctx);

    if (use_binary)
    {
        infs = std::fopen(bin_filename.c_str(), "rb");

        size_t buf_len = sizeof(magic);
        auto buf = std::make_unique<char[]>(buf_len);

        std::memset(buf.get(), 0, buf_len);

        std::fread(buf.get(), buf_len, 1, infs);

        if (std::strcmp(magic, buf.get()) == 0)
        {
            auto parent_vpeg_ctx = vpeg::context_t::current_ctx;

            vpeg::context_t::current_ctx = nullptr;

            while(!std::feof(infs))
            {
                size_t len;

                std::fread(&len, sizeof(len), 1, infs);

                if (std::feof(infs))  break;        //- WTF ?!?!?

                if (buf_len < len)
                {
                    buf = std::make_unique<char[]>(len);

                    buf_len = len;
                }

                std::fread(buf.get(), len, 1, infs);

                lctx.unit_buffer = LLVMCreateMemoryBufferWithMemoryRange(buf.get(), len, "unit_buffer", false);

                lctx.run_unit_action();
            }

            vpeg::context_t::current_ctx = parent_vpeg_ctx;
        }
        else
        {
            std::fclose(infs);

            use_binary = false;
        }
    }

    if (!use_binary)
    {
        infs = std::fopen(src_filename.c_str(), "rb");

        std::FILE *outfs = std::fopen(bin_filename.c_str(), "wb");

        {   char buf[sizeof(magic)];

            std::memset(buf, 0, sizeof(magic));

            std::fwrite(buf, sizeof(magic), 1, outfs);
        }

        {   vpeg::context_t pctx(infs, voidc_grammar);

            auto parent_vpeg_ctx = vpeg::context_t::current_ctx;

            vpeg::context_t::current_ctx = &pctx;

            while(auto unit = parse_unit(pctx))
            {
                unit->accept(voidc_compiler);

                unit.reset();

                if (lctx.unit_buffer)
                {
                    size_t len = LLVMGetBufferSize(lctx.unit_buffer);

                    std::fwrite((char *)&len, sizeof(len), 1, outfs);

                    std::fwrite(LLVMGetBufferStart(lctx.unit_buffer), len, 1, outfs);

                    lctx.run_unit_action();
                }
            }

            vpeg::context_t::current_ctx = parent_vpeg_ctx;
        }

        std::fseek(outfs, 0, SEEK_SET);

        std::fwrite(magic, sizeof(magic), 1, outfs);

        std::fclose(outfs);
    }

    std::fclose(infs);
}

//--------------------------------------------------------------------
void
voidc_import(const char *name)
{
    auto voidc  = voidc_global_ctx_t::voidc;
    auto target = voidc_global_ctx_t::target;

    if (target == voidc)
    {
        v_import(name);

        return;
    }

    //- target != voidc

    voidc_global_ctx_t::target = voidc;

    v_import(name);

    voidc_global_ctx_t::target = target;
}

//--------------------------------------------------------------------
void
voidc_guard_import(const char *errmsg)
{
    if (voidc_global_ctx_t::target == voidc_global_ctx_t::voidc)  return;

    throw  std::runtime_error(std::string("voidc_guard_import: ") + errmsg);
}


VOIDC_DLLEXPORT_END

}   //- extern "C"


//--------------------------------------------------------------------
//- As is...
//--------------------------------------------------------------------
int
main(int argc, char *argv[])
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

        auto import_f_type = LLVMFunctionType(gctx.void_type, &char_ptr_type, 1, false);

        gctx.add_symbol_type("v_import",           import_f_type);
        gctx.add_symbol_type("voidc_import",       import_f_type);
        gctx.add_symbol_type("voidc_guard_import", import_f_type);      //- Kind of...
    }

    v_ast_static_initialize();
    voidc_visitor_t::static_initialize();
    vpeg::grammar_t::static_initialize();
    vpeg::context_t::static_initialize();

    voidc_grammar = make_voidc_grammar();

    vpeg::grammar_t current_grammar = voidc_grammar;

    for (auto &src : sources)
    {
        std::string src_name = src;

        std::FILE *istr;

        if (src == "-")
        {
            src_name = "<stdin>";

            istr = stdin;
        }
        else
        {
            if (!fs::exists(src_name))
            {
                throw std::runtime_error("Source file not found: " + src_name);
            }

            istr = std::fopen(src_name.c_str(), "rb");
        }

        voidc_local_ctx_t lctx(src_name, gctx);

        vpeg::context_t pctx(istr, current_grammar);

        vpeg::context_t::current_ctx = &pctx;

        while(auto unit = parse_unit(pctx))
        {
            unit->accept(voidc_compiler);

            unit.reset();

            lctx.run_unit_action();
        }

        vpeg::context_t::current_ctx = nullptr;     //- ?

        current_grammar = pctx.grammar;

        if (src != "-")   std::fclose(istr);
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
    voidc_visitor_t::static_terminate();
    v_ast_static_terminate();

    utility::static_terminate();
    voidc_global_ctx_t::static_terminate();

    return 0;
}


