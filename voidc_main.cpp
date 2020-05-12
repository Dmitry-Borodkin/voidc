#include <iostream>
#include <fstream>

#include <set>
#include <list>
#include <memory>
#include <cassert>
#include <cstring>
#include <filesystem>

#include "voidc_llvm.h"
#include "vpeg_context.h"
#include "vpeg_voidc.h"

#include <llvm-c/Core.h>
#include <llvm-c/Support.h>


//---------------------------------------------------------------------
namespace fs = std::filesystem;


//---------------------------------------------------------------------
static std::list<fs::path> import_paths =
{
    "."
};

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
static
void voidc_intrinsic_import(void *void_cctx, const char *name)
{
    auto *parent_cctx = (compile_ctx_t *)void_cctx;

    fs::path src_filename = name;

//  fs::path parent_path = fs::path(parent_cctx->filename.cpp_str()).parent_path();
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

        {   vpeg::grammar_t gr = make_voidc_grammar();

            vpeg::context_t ctx(infs, gr);

            for(;;)
            {
                auto v = ctx.grammar.parse("unit", ctx);

                if (!ctx.is_ok()) break;

                if (auto unit = std::any_cast<std::shared_ptr<const ast_unit_t>>(v))
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
        }

        outfs.seekp(0);

        outfs.write(magic, sizeof(magic));

        outfs.close();
    }

    infs.close();
}

//---------------------------------------------------------------------
static
void v_import(compile_ctx_t &cctx, const std::shared_ptr<const ast_arg_list_t> &args)
{
    cctx.call_intrinsic_helper("voidc_intrinsic_import", args);
}


//---------------------------------------------------------------------
//- As is...
//---------------------------------------------------------------------
int main()
{
    compile_ctx_t::static_initialize();

    compile_ctx_t cctx("<stdin>");

    {   LLVMTypeRef args[] =
        {
            LLVMPointerType(cctx.void_type, 0),
            LLVMPointerType(cctx.char_type, 0)
        };

        cctx.symbol_types["voidc_intrinsic_import"] = LLVMFunctionType(cctx.void_type, args, 2, false);
        LLVMAddSymbol("voidc_intrinsic_import", (void *)voidc_intrinsic_import);

        cctx.intrinsics["v_import"] = &v_import;
    }

    vpeg::grammar_t::static_initialize();

    {   vpeg::grammar_t gr = make_voidc_grammar();

        vpeg::context_t ctx(std::cin, gr);

        for(;;)
        {
            auto v = ctx.grammar.parse("unit", ctx);

            if (!ctx.is_ok()) break;

            if (auto unit = std::any_cast<std::shared_ptr<const ast_unit_t>>(v))
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
    }

//    for (auto &it : compile_ctx_t::symbol_types)
//    {
//        printf("%s : ", it.first.c_str());
//
//        fflush(stdout);     //- WTF ?
//
//        auto s = LLVMPrintTypeToString(it.second);
//
//        printf("%s\n", s);
//
//        LLVMDisposeMessage(s);
//    }


    vpeg::grammar_t::static_terminate();

    compile_ctx_t::static_terminate();

    return 0;
}


