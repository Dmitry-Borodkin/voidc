//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
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

#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
//- Some utility...
//---------------------------------------------------------------------
static vpeg::grammar_t voidc_grammar;

static ast_unit_sptr_t
parse_unit(vpeg::context_t &pctx)
{
    static const auto unit_q = v_quark_from_string("unit");

    auto ret = pctx.grammar.parse(unit_q, pctx);

    pctx.memo.clear();

    if (auto unit = std::any_cast<ast_unit_sptr_t>(&ret))  return *unit;

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
static inline
std::FILE *
my_fopen(const fs::path &fpath, bool write=false)
{

#ifdef _WIN32

    return _wfopen(fpath.c_str(), (write ? L"wb" : L"rb"));

#else

    return std::fopen(fpath.c_str(), (write ? "wb" : "rb"));

#endif

}


//--------------------------------------------------------------------
static
const char magic[8] = ".voidc\n";

enum import_state_t
{
    ist_unknown,        //- ...
    ist_good,           //- Usable binary
    ist_bad             //- Need (re)compile
};

static
std::map<std::string, import_state_t> import_state;

static bool
check_import_state(const fs::path &src_filename)
{
    auto src_filename_str = src_filename.string();

    {   auto it = import_state.find(src_filename_str);

        if (it != import_state.end())
        {
            return  (it->second != ist_bad);            //- "unknown" => kinda "good"...
        }
    }

    //- Perform check

    import_state[src_filename_str] = ist_unknown;

    fs::path bin_filename = src_filename;

    bin_filename += "c";

    bool use_binary = true;

    //- First, check bin_filename itself

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

    if (!use_binary)
    {
        import_state[src_filename_str] = ist_bad;

        return false;
    }

    //- Second, check for header ...

    std::FILE *infs = my_fopen(bin_filename);

    size_t buf_len = sizeof(magic);
    auto buf = std::make_unique<char[]>(buf_len);

    std::memset(buf.get(), 0, buf_len);

    std::fread(buf.get(), buf_len, 1, infs);

    if (std::strcmp(magic, buf.get()) != 0)
    {
        import_state[src_filename_str] = ist_bad;

        std::fclose(infs);

        return false;
    }

    //- Now, check for imports ...

    std::fread(buf.get(), sizeof(size_t), 1, infs);

    std::fseek(infs, *((size_t *)buf.get()), SEEK_SET);

    fs::path parent_path = src_filename.parent_path();

    while(use_binary)
    {
        size_t len;

        std::fread(&len, sizeof(len), 1, infs);

        if (std::feof(infs))  break;        //- WTF ?!?!?

        if (len == 0)  break;

        if (buf_len < len)
        {
            buf = std::make_unique<char[]>(len);

            buf_len = len;
        }

        std::fread(buf.get(), len, 1, infs);

        std::string name(buf.get(), len);

        fs::path imp_filename = fs::u8path(name);

        imp_filename = find_file_for_import(parent_path, imp_filename);

        if (!fs::exists(imp_filename))
        {
            throw std::runtime_error("Import file not found: " + imp_filename.string());
        }

        use_binary = check_import_state(imp_filename);
    }

    std::fclose(infs);

    import_state[src_filename_str] = (use_binary ? ist_good : ist_bad);

    return use_binary;
}


//--------------------------------------------------------------------
//- ...
//--------------------------------------------------------------------
struct out_binary_t
{
    explicit out_binary_t(const fs::path &filename)
      : out_path(filename),
        tmp_path(filename)
    {
        tmp_path += "_XXXXXX";

        fs::path::string_type tmp_name = tmp_path.native();

#ifdef _WIN32

        int fd = -1;

        static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

        int len = int(tmp_name.size());

        auto *template_name = tmp_name.data();

        for (int i=0; i >= 0; i++)
        {
            for (int j=len-6; j < len; j++)
            {
                template_name[j] = letters[std::rand()/((RAND_MAX + 1u)/62)];
            }

            fd = _wsopen(template_name,
                         _O_RDWR|_O_CREAT|_O_EXCL|_O_BINARY,
                         _SH_DENYRW,
                         _S_IREAD|_S_IWRITE
                        );

            if (fd != -1  ||  errno != EEXIST) break;
        }

        assert(fd != -1);       //- ?..

#else

        int fd = mkstemp(tmp_name.data());

#endif

        tmp_path = tmp_name.c_str();

        f = ::fdopen(fd, "wb");
    }

    ~out_binary_t()
    {
        std::fclose(f);

        fs::rename(tmp_path, out_path);
    }

public:
    FILE *f = nullptr;

private:
    const fs::path out_path;

    fs::path tmp_path;
};


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

    parent_lctx->imports.insert(name);

    fs::path src_filename = fs::u8path(name);

    fs::path parent_path = fs::path(parent_lctx->filename).parent_path();

    src_filename = find_file_for_import(parent_path, src_filename);

    auto src_filename_str = src_filename.string();

    if (!fs::exists(src_filename))
    {
        throw std::runtime_error("Import file not found: " + src_filename_str);
    }

    {   auto &tctx = *voidc_global_ctx_t::target;

        if (tctx.imports.count(src_filename_str)) return;

        tctx.imports.insert(src_filename_str);
    }

    fs::path bin_filename = src_filename;

    bin_filename += "c";

    bool use_binary = check_import_state(src_filename);

    std::FILE *infs;

    voidc_local_ctx_t lctx(src_filename_str, vctx);

    if (use_binary)
    {
        infs = my_fopen(bin_filename);

        size_t buf_len = sizeof(magic);
        auto buf = std::make_unique<char[]>(buf_len);

        std::memset(buf.get(), 0, buf_len);

        std::fread(buf.get(), buf_len, 1, infs);

        assert(std::strcmp(magic, buf.get()) == 0);

        std::fseek(infs, sizeof(size_t), SEEK_CUR);         //- Skip pointer to imports

        auto parent_vpeg_ctx = vpeg::context_t::current_ctx;

        vpeg::context_t::current_ctx = nullptr;

        while(!std::feof(infs))
        {
            size_t len;

            std::fread(&len, sizeof(len), 1, infs);

            if (std::feof(infs))  break;        //- WTF ?!?!?

            if (len == 0)  break;

            if (buf_len < len)
            {
                buf = std::make_unique<char[]>(len);

                buf_len = len;
            }

            std::fread(buf.get(), len, 1, infs);

            lctx.unit_buffer = LLVMCreateMemoryBufferWithMemoryRange(buf.get(), len, "unit_buffer", false);

            lctx.run_unit_action();

            LLVMDisposeMemoryBuffer(lctx.unit_buffer);

            lctx.unit_buffer = nullptr;
        }

        vpeg::context_t::current_ctx = parent_vpeg_ctx;
    }
    else        //- !use_binary
    {
        infs = my_fopen(src_filename);

        out_binary_t out_binary(bin_filename);

        const auto &outfs = out_binary.f;

        {   char buf[sizeof(magic)];

            std::memset(buf, 0, sizeof(magic));

            std::fwrite(buf, sizeof(magic), 1, outfs);

            std::fwrite(buf, sizeof(size_t), 1, outfs);         //- Pointer to imports...
        }

        {   vpeg::context_t pctx(infs, voidc_grammar);

            auto parent_vpeg_ctx = vpeg::context_t::current_ctx;

            vpeg::context_t::current_ctx = &pctx;

            while(auto unit = parse_unit(pctx))
            {
                unit->accept(voidc_compiler, nullptr);          //- aux ?

                unit.reset();

                if (lctx.unit_buffer)   lctx.run_unit_action();

                if (lctx.unit_buffer)   //- Sic!
                {
                    size_t len = LLVMGetBufferSize(lctx.unit_buffer);

                    std::fwrite((char *)&len, sizeof(len), 1, outfs);

                    std::fwrite(LLVMGetBufferStart(lctx.unit_buffer), len, 1, outfs);

                    LLVMDisposeMemoryBuffer(lctx.unit_buffer);

                    lctx.unit_buffer = nullptr;
                }
            }

            vpeg::context_t::current_ctx = parent_vpeg_ctx;
        }

        size_t len = 0;

        std::fwrite((char *)&len, sizeof(len), 1, outfs);       //- End of units

        size_t imports_pos = std::ftell(outfs);

        for (auto &it : lctx.imports)
        {
            len = it.length();

            std::fwrite((char *)&len, sizeof(len), 1, outfs);

            std::fwrite(it.data(), len, 1, outfs);
        }

        len = 0;

        std::fwrite((char *)&len, sizeof(len), 1, outfs);       //- End of imports

        //- Finish it...

        std::fseek(outfs, 0, SEEK_SET);

        std::fwrite(magic, sizeof(magic), 1, outfs);

        std::fwrite((char *)&imports_pos, sizeof(imports_pos), 1, outfs);
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
voidc_guard_target(const char *errmsg)
{
    if (voidc_global_ctx_t::target == voidc_global_ctx_t::voidc)  return;

    throw  std::runtime_error(std::string("voidc_guard_target: ") + errmsg);
}


VOIDC_DLLEXPORT_END

}   //- extern "C"


//--------------------------------------------------------------------
//- As is...
//--------------------------------------------------------------------
int
main(int argc, char *argv[])
{
#ifdef _WIN32
    std::srand(std::time(nullptr));
#endif

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

    {   v_type_t *import_f_type = gctx.make_function_type(gctx.void_type, &gctx.char_ptr_type, 1, false);

        gctx.add_symbol_type("v_import",           import_f_type);
        gctx.add_symbol_type("voidc_import",       import_f_type);
        gctx.add_symbol_type("voidc_guard_target", import_f_type);      //- Kind of...
    }

    v_ast_static_initialize();
    voidc_visitor_t::static_initialize();
    vpeg::grammar_t::static_initialize();
    vpeg::context_t::static_initialize();

    voidc_global_ctx_t::voidc->flush_unit_symbols();

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
            fs::path src_path = fs::u8path(src_name);

            if (!fs::exists(src_path))
            {
                throw std::runtime_error("Source file not found: " + src_name);
            }

            istr = my_fopen(src_path);
        }

        voidc_local_ctx_t lctx(src_name, gctx);

        vpeg::context_t pctx(istr, current_grammar);

        vpeg::context_t::current_ctx = &pctx;

        while(auto unit = parse_unit(pctx))
        {
            unit->accept(voidc_compiler, nullptr);      //- aux ?

            unit.reset();

            lctx.run_unit_action();

            LLVMDisposeMemoryBuffer(lctx.unit_buffer);

            lctx.unit_buffer = nullptr;
        }

        vpeg::context_t::current_ctx = nullptr;     //- ?

        current_grammar = pctx.grammar;

        if (src != "-")   std::fclose(istr);
    }

    vpeg::context_t::static_terminate();
    vpeg::grammar_t::static_terminate();
    voidc_visitor_t::static_terminate();
    v_ast_static_terminate();

    utility::static_terminate();
    voidc_global_ctx_t::static_terminate();

    return 0;
}


