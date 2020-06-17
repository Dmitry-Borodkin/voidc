#include "voidc_llvm.h"
#include "voidc_util.h"
#include "vpeg_context.h"
#include "vpeg_voidc.h"

#include <iostream>
#include <fstream>
#include <set>
#include <list>
#include <memory>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <filesystem>

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


//---------------------------------------------------------------------
namespace fs = std::filesystem;


//---------------------------------------------------------------------
static std::list<fs::path> import_paths;

#ifdef _WIN32
#define PATHSEP ';'
#else
#define PATHSEP ':'
#endif

void import_paths_initialize(void)
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
}

#undef PATHSEP

//---------------------------------------------------------------------
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


//---------------------------------------------------------------------
static std::set<fs::path> already_imported;

//---------------------------------------------------------------------
extern "C"
{

static
void v_import(const char *name)
{
    auto *parent_cctx = compile_ctx_t::current_ctx;

    fs::path src_filename = name;

    fs::path parent_path = fs::path(parent_cctx->filename).parent_path();

    src_filename = find_file_for_import(parent_path, src_filename);

    assert(fs::exists(src_filename));

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

    compile_ctx_t cctx(src_filename.string());

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

                cctx.unit_buffer = LLVMCreateMemoryBufferWithMemoryRange(buf.get(), len, "unit_buffer", false);

                cctx.run_unit_action();
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

        {   vpeg::context_t pctx(infs, voidc_grammar, cctx);

            auto parent_vpeg_ctx = vpeg::context_t::current_ctx;

            vpeg::context_t::current_ctx = &pctx;

            for(;;)
            {
                auto v = parse_unit(pctx);

                if (!pctx.is_ok())  break;

                if (auto unit = std::any_cast<ast_unit_ptr_t>(v))
                {
                    unit->compile(cctx);

                    unit.reset();

                    if (cctx.unit_buffer)
                    {
                        size_t len = LLVMGetBufferSize(cctx.unit_buffer);

                        outfs.write((char *)&len, sizeof(len));

                        outfs.write(LLVMGetBufferStart(cctx.unit_buffer), len);

                        cctx.run_unit_action();
                    }
                }
                else
                {
                    assert(false && "Unit parse error!");
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


//---------------------------------------------------------------------
//- As is...
//---------------------------------------------------------------------
int main()
{
    import_paths_initialize();

    compile_ctx_t::static_initialize();
    utility::static_initialize();

    compile_ctx_t cctx("<stdin>");

    {   auto char_ptr_type = LLVMPointerType(cctx.char_type, 0);

        v_add_symbol("v_import",
                     LLVMFunctionType(cctx.void_type, &char_ptr_type, 1, false),
                     (void *)v_import
                    );
    }

    v_ast_static_initialize();
    vpeg::grammar_t::static_initialize();
    vpeg::context_t::static_initialize();

    voidc_grammar = make_voidc_grammar();

    {   vpeg::context_t pctx(std::cin, voidc_grammar, cctx);

        vpeg::context_t::current_ctx = &pctx;

        for(;;)
        {
            auto v = parse_unit(pctx);

            if (!pctx.is_ok())  break;

            if (auto unit = std::any_cast<ast_unit_ptr_t>(v))
            {
                unit->compile(cctx);

                unit.reset();

                cctx.run_unit_action();
            }
            else
            {
                assert(false && "Unit parse error!");
            }
        }

        vpeg::context_t::current_ctx = nullptr;     //- ?
    }

#if 0

    for (auto &it : compile_ctx_t::symbol_types)
    {
        printf("%s : ", it.first.c_str());

        fflush(stdout);     //- WTF ?

        auto s = LLVMPrintTypeToString(it.second);

        printf("%s\n", s);

        LLVMDisposeMessage(s);
    }

#endif

    vpeg::context_t::static_terminate();
    vpeg::grammar_t::static_terminate();
    v_ast_static_terminate();

    utility::static_terminate();
    compile_ctx_t::static_terminate();

    return 0;
}


