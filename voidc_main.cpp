#include <iostream>
#include <fstream>

#include <set>
#include <list>
#include <memory>
#include <cassert>
#include <cstring>
#include <filesystem>

#include "voidc_llvm.h"

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
fs::path find_file_for_import(const fs::path &filename)
{
    for (auto it : import_paths)
    {
        auto p = it / filename;

        if (fs::exists(p))  return  fs::absolute(p);
    }

    return  "";
}


//---------------------------------------------------------------------
static std::set<fs::path> already_imported;


//---------------------------------------------------------------------
static
void v_import(const char *name)
{
    fs::path src_filename = name;

    src_filename += ".void";

    src_filename = find_file_for_import(src_filename);

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

    compile_ctx_t cctx;

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

                cctx.unit_buffer = LLVMCreateMemoryBufferWithMemoryRangeCopy(buf.get(), len, "unit_buffer");

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

        ast_builder_t auxil(infs);

        voidc_context_t *ctx = voidc_create(&auxil);

        value_t value;

        while(voidc_parse(ctx, &value))
        {
            if (auto unit = std::dynamic_pointer_cast<const ast_unit_t>(*value))
            {
                unit->compile(cctx);

                auxil.clear();

                size_t len = LLVMGetBufferSize(cctx.unit_buffer);

                outfs.write((char *)&len, sizeof(len));

                outfs.write(LLVMGetBufferStart(cctx.unit_buffer), len);

                cctx.run_unit_action();
            }
            else
            {
                assert(false && "Unit parse error!");
            }
        }

        voidc_destroy(ctx);

        outfs.seekp(0);

        outfs.write(magic, sizeof(magic));

        outfs.close();
    }

    infs.close();
}


//---------------------------------------------------------------------
//- As is...
//---------------------------------------------------------------------
int main()
{
    compile_ctx_t::initialize();

    compile_ctx_t cctx;

    {   auto arg = LLVMPointerType(cctx.char_type, 0);

        cctx.symbol_types["v_import"] = LLVMFunctionType(cctx.void_type, &arg, 1, false);
        LLVMAddSymbol("v_import", (void *)v_import);
    }

    ast_builder_t auxil(std::cin);

    voidc_context_t *ctx = voidc_create(&auxil);

    value_t value;

    while(voidc_parse(ctx, &value))
    {
        if (auto unit = std::dynamic_pointer_cast<const ast_unit_t>(*value))
        {
            unit->compile(cctx);

            auxil.clear();

            cctx.run_unit_action();
        }
        else
        {
            assert(false && "Unit parse error!");
        }
    }

    voidc_destroy(ctx);


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


    compile_ctx_t::terminate();

    return 0;
}


