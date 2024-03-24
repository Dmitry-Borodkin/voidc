//---------------------------------------------------------------------
//- Copyright (C) 2020-2024 Dmitry Borodkin <borodkin.dn@gmail.com>
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
#include <windows.h>
#endif

#include <llvm-c/Core.h>


//---------------------------------------------------------------------
//- Some utility...
//---------------------------------------------------------------------
#ifndef NDEBUG

extern "C"
const char *__asan_default_options()
{
    return "handle_abort=1";
}

#endif


//---------------------------------------------------------------------
static vpeg::grammar_data_t voidc_grammar;

static ast_unit_t
parse_unit(void)
{
    auto &pctx = *vpeg::context_data_t::current_ctx;

    static const auto unit_q = v_quark_from_string("unit");

    auto ret = pctx.grammar.parse(unit_q, pctx);

    pctx.memo.clear();

    if (auto unit = std::any_cast<ast_unit_t>(&ret))  return *unit;

    size_t line, column;

    pctx.get_line_column(pctx.get_buffer_size(), line, column);

    auto &vctx = *voidc_global_ctx_t::voidc;        //- Sic!!!
    auto &lctx = *vctx.local_ctx;

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
import_paths_initialize(fs::path &exe_path)
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

    if (fs::exists(exe_path))
    {
        fs::path p(exe_path);

        p = fs::canonical(p);
        p = p.parent_path();

        import_paths.push_back(p);

        //- Dirty hack...

        import_paths.push_back(p / "..");               //- WTF ?!?!?
    }
}

#undef PATHSEP

//--------------------------------------------------------------------
static fs::path
find_file_for_import(const fs::path &parent, const fs::path &filename)
{
    fs::path ret;

    if (filename.is_relative())
    {
        auto p = parent / filename;

        if (fs::exists(p))
        {
            ret = p;
        }
        else
        {
            for (auto it : import_paths)
            {
                auto p = it / filename;

                if (fs::exists(p))
                {
                    ret = p;

                    break;
                }
            }
        }
    }
    else if (fs::exists(filename))
    {
        ret = filename;
    }

    if (fs::is_directory(ret))
    {
        ret = ret / "__import__.void";
    }

    if (fs::is_regular_file(ret))
    {
        return  fs::canonical(ret);
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

static inline
void
my_fread(void* buf, std::size_t sz, std::size_t cn, std::FILE* f)
{
    auto dummy = std::fread(buf, sz, cn, f);
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
    auto src_filename_str = src_filename.generic_u8string();

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

    fs::file_time_type bin_time;

    //- First, check bin_filename itself

    if (!fs::exists(bin_filename))
    {
        use_binary = false;
    }
    else
    {
        auto st  = fs::last_write_time(src_filename);
        bin_time = fs::last_write_time(bin_filename);

        if (st > bin_time)  use_binary = false;
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

    my_fread(buf.get(), buf_len, 1, infs);

    if (std::strcmp(magic, buf.get()) != 0)
    {
        import_state[src_filename_str] = ist_bad;

        std::fclose(infs);

        return false;
    }

    //- Now, check for imports ...

    my_fread(buf.get(), sizeof(size_t), 1, infs);

    std::fseek(infs, *((size_t *)buf.get()), SEEK_SET);

    fs::path parent_path = src_filename.parent_path();

    while(use_binary)
    {
        size_t len;

        my_fread(&len, sizeof(len), 1, infs);

        if (std::feof(infs))  break;        //- WTF ?!?!?

        if (len == 0)  break;

        if (buf_len < len)
        {
            buf = std::make_unique<char[]>(len);

            buf_len = len;
        }

        my_fread(buf.get(), len, 1, infs);

        std::string name(buf.get(), len);

        fs::path imp_filename = fs::u8path(name);

        imp_filename = find_file_for_import(parent_path, imp_filename);

        if (!fs::exists(imp_filename))
        {
            throw std::runtime_error("Import file not found: " + name);
        }

        use_binary = check_import_state(imp_filename);

        if (use_binary)
        {
            fs::path bin_impfile = imp_filename;

            bin_impfile += "c";

            auto bt = fs::last_write_time(bin_impfile);

            if (bt > bin_time)  use_binary = false;
        }
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
                template_name[j] = letters[std::rand()/(RAND_MAX/61)];
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
static bool trace_imports = false;

static void
v_import_helper(const char *name, bool _export)
{
    auto &vctx = *voidc_global_ctx_t::voidc;

    auto *parent_lctx = static_cast<voidc_local_ctx_t *>(vctx.local_ctx);

    parent_lctx->imports.insert(name);

    fs::path src_filename = fs::u8path(name);

    fs::path parent_path = fs::path(parent_lctx->filename).parent_path();

    src_filename = find_file_for_import(parent_path, src_filename);

    auto src_filename_str = src_filename.generic_u8string();

    if (!fs::exists(src_filename))
    {
        throw std::runtime_error(std::string("Import file not found: ") + name);
    }

    base_compile_ctx_t::declarations_t *export_decls     = nullptr;

    auto &tctx = *voidc_global_ctx_t::target;

    auto *target_lctx = tctx.local_ctx;

    {   auto [it, ok] = tctx.imported.try_emplace(src_filename_str);

        if (!ok)    //- Already imported
        {
            auto res_i = target_lctx->imported.insert(src_filename_str);

            if (res_i.second) target_lctx->decls.insert(it->second);

            if (_export  &&  target_lctx->export_decls)
            {
                auto res_e = target_lctx->exported.insert(src_filename_str);

                if (res_e.second) target_lctx->export_decls->insert(it->second);
            }

            return;     //- Sic!
        }

        export_decls = &it->second;
    }

    fs::path bin_filename = src_filename;

    bin_filename += "c";

    bool use_binary = check_import_state(src_filename);

    std::FILE *infs;

    voidc_local_ctx_t lctx(vctx);

    lctx.filename = src_filename_str;

    if (&tctx == &vctx)
    {
        lctx.export_decls = export_decls;
    }

    if (use_binary)
    {
        infs = my_fopen(bin_filename);

        size_t buf_len = sizeof(magic);
        auto buf = std::make_unique<char[]>(buf_len);

        std::memset(buf.get(), 0, buf_len);

        my_fread(buf.get(), buf_len, 1, infs);

        assert(std::strcmp(magic, buf.get()) == 0);

        std::fseek(infs, sizeof(size_t), SEEK_CUR);         //- Skip pointer to imports

        auto parent_vpeg_ctx = vpeg::context_data_t::current_ctx;

        vpeg::context_data_t::current_ctx = nullptr;

        while(!std::feof(infs))
        {
            size_t len;

            my_fread(&len, sizeof(len), 1, infs);

            if (std::feof(infs))  break;        //- WTF ?!?!?

            if (len == 0)  break;

            if (buf_len < len)
            {
                buf = std::make_unique<char[]>(len);

                buf_len = len;
            }

            my_fread(buf.get(), len, 1, infs);

            lctx.unit_buffer = LLVMCreateMemoryBufferWithMemoryRange(buf.get(), len, "unit_buffer", false);

            lctx.run_unit_action();

            LLVMDisposeMemoryBuffer(lctx.unit_buffer);

            lctx.unit_buffer = nullptr;
        }

        vpeg::context_data_t::current_ctx = parent_vpeg_ctx;
    }
    else        //- !use_binary
    {
        if (trace_imports)  printf("start:  %s\n", src_filename_str.c_str());

        infs = my_fopen(src_filename);

        out_binary_t out_binary(bin_filename);

        const auto &outfs = out_binary.f;

        {   char buf[sizeof(magic)];

            std::memset(buf, 0, sizeof(magic));

            std::fwrite(buf, sizeof(magic), 1, outfs);

            std::fwrite(buf, sizeof(size_t), 1, outfs);         //- Pointer to imports...
        }

        {   auto parent_vpeg_ctx = vpeg::context_data_t::current_ctx;

            vpeg::context_data_t::current_ctx = std::make_shared<vpeg::context_data_t>(infs, voidc_grammar);

            while(auto unit = parse_unit())
            {
                voidc_visitor_data_t::visit(lctx.compiler, unit);

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

            vpeg::context_data_t::current_ctx = parent_vpeg_ctx;
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

        if (trace_imports)  printf("finish: %s\n", src_filename_str.c_str());
    }

    std::fclose(infs);

    target_lctx->decls.insert(*export_decls);

    target_lctx->imported.insert(src_filename_str);

    if (_export  &&  target_lctx->export_decls)
    {
        target_lctx->export_decls->insert(*export_decls);

        target_lctx->exported.insert(src_filename_str);
    }
}

//--------------------------------------------------------------------
static void
voidc_import_helper(const char *name, bool _export)
{
    auto voidc  = voidc_global_ctx_t::voidc;
    auto target = voidc_global_ctx_t::target;

    if (target == voidc)
    {
        v_import_helper(name, _export);

        return;
    }

    //- target != voidc

    voidc_global_ctx_t::target = voidc;

    v_import_helper(name, _export);

    voidc_global_ctx_t::target = target;
}


//---------------------------------------------------------------------
static void
v_local_import(void *, const visitor_t *vis, const ast_base_t *self)
{
    auto &gctx = *voidc_global_ctx_t::target;
    auto &lctx = *gctx.local_ctx;

    lctx.push_builder_ip();

    auto &call = static_cast<const ast_expr_call_data_t &>(**self);

    auto &args = call.arg_list;

    auto &r = static_cast<const ast_expr_string_data_t &>(*args->data[0]);

    v_import_helper(r.string.c_str(), false);

    lctx.pop_builder_ip();
}


//--------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_FUNCTION

//--------------------------------------------------------------------
void
v_export_import(const char *name)
{
    v_import_helper(name, true);
}

//--------------------------------------------------------------------
void
voidc_export_import(const char *name)
{
    voidc_import_helper(name, true);
}

//--------------------------------------------------------------------
void
v_import(const char *name)
{
    v_import_helper(name, false);
}

//--------------------------------------------------------------------
void
voidc_import(const char *name)
{
    voidc_import_helper(name, false);
}


//--------------------------------------------------------------------
void
voidc_guard_target(const char *errmsg)
{
    if (voidc_global_ctx_t::target == voidc_global_ctx_t::voidc)  return;

    throw  std::runtime_error(std::string("voidc_guard_target: ") + errmsg);
}


//--------------------------------------------------------------------
void
v_find_file_for_import(std::string *ret, const char *parent, const char *filename)
{
    auto _parent   = fs::u8path(parent);
    auto _filename = fs::u8path(filename);

    auto path = find_file_for_import(_parent, _filename);

    *ret = path.generic_u8string();
}


//--------------------------------------------------------------------
std::FILE *
v_fopen(const char *filename, const char *prop)
{

#ifdef _WIN32

    fs::path fpath = fs::u8path(filename);

    int len = std::strlen(prop);

    auto wprop = std::make_unique<wchar_t[]>(len+1);

    for (int i=0; i<=len; ++i)  wprop[i] = (wchar_t)prop[i];        //- ASCII only!

    return _wfopen(fpath.c_str(), wprop.get());

#else

    return std::fopen(filename, prop);

#endif

}

//--------------------------------------------------------------------
int
v_fclose(std::FILE *f)
{
    return std::fclose(f);
}

//--------------------------------------------------------------------
std::FILE *
v_popen(const char *command, const char *prop)
{
    return ::popen(command, prop);
}

//--------------------------------------------------------------------
int
v_pclose(std::FILE *p)
{
    return ::pclose(p);
}


VOIDC_DLLEXPORT_END

}   //- extern "C"


//--------------------------------------------------------------------
static void
voidc_flush_output(void)
{
    fflush(stdout);     //- WTF?
    fflush(stderr);     //- WTF?
}


//--------------------------------------------------------------------
//- As is...
//--------------------------------------------------------------------
int
main(int argc, char *argv[])
{
    std::ios::sync_with_stdio(true);

    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);

    std::atexit(voidc_flush_output);

#ifdef _WIN32
    std::srand(std::time(nullptr));
#endif

    {   fs::path exe_path;

#ifdef _WIN32
        wchar_t path[_MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, path, _MAX_PATH);

        exe_path = path;
#else
        exe_path = "/proc/self/exe";
#endif

        import_paths_initialize(exe_path);
    }

    std::list<std::string> sources;

    while (optind < argc)
    {
        char c;

        if ((c = getopt(argc, argv, "-I:s:T")) != -1)
        {
            //- Option argument

            switch (c)
            {
            case 'I':
                import_paths.push_back(optarg);
                break;

            case 's':
                sources.push_back(optarg);
                c = -1;
                break;

            case 'T':
                trace_imports = true;
                break;

            case 1:
                sources.push_back(optarg);
                break;

            default:
                break;
            }
        }

        if (c == -1)  break;
    }

    if (sources.empty())  sources.push_back("-");

    voidc_global_ctx_t::static_initialize();

    auto &gctx = *voidc_global_ctx_t::voidc;

    utility::static_initialize();

    {   v_type_t *import_f_type = gctx.make_function_type(gctx.void_type, &gctx.char_ptr_type, 1, false);

        auto q = v_quark_from_string;

        gctx.decls.symbols_insert({q("v_export_import"),     import_f_type});
        gctx.decls.symbols_insert({q("voidc_export_import"), import_f_type});
        gctx.decls.symbols_insert({q("v_import"),            import_f_type});
        gctx.decls.symbols_insert({q("voidc_import"),        import_f_type});
        gctx.decls.symbols_insert({q("voidc_guard_target"),  import_f_type});       //- Kind of...

        gctx.decls.intrinsics_insert({q("v_local_import"), {(void *)v_local_import, nullptr}});

#ifdef _WIN32
        gctx.add_symbol_value(q("stdout"), stdout);
        gctx.add_symbol_value(q("stderr"), stderr);

        gctx.add_symbol_value(q("getopt"), (void *)&getopt);
        gctx.add_symbol_value(q("optind"), &optind);
        gctx.add_symbol_value(q("optarg"), &optarg);
#endif

        gctx.add_symbol_value(q("voidc_argc"), &argc);
        gctx.add_symbol_value(q("voidc_argv"), &argv);

        gctx.add_symbol_value(q("voidc_import_paths"), &import_paths);      //- Sic!
    }

    v_ast_static_initialize();
    voidc_visitor_data_t::static_initialize();
    vpeg::grammar_data_t::static_initialize();
    vpeg::context_data_t::static_initialize();

    voidc_global_ctx_t::voidc->flush_unit_symbols();

    voidc_grammar = make_voidc_grammar();

    {   vpeg::grammar_data_t current_grammar = voidc_grammar;

        voidc_local_ctx_t lctx(gctx);

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

                src_path = fs::canonical(src_path);

                if (!fs::exists(src_path))
                {
                    throw std::runtime_error("Source file not found: " + src_name);
                }

                istr = my_fopen(src_path);

                src_name = src_path.generic_u8string();
            }

            lctx.filename = src_name;

            vpeg::context_data_t::current_ctx = std::make_shared<vpeg::context_data_t>(istr, current_grammar);

            {   auto &pctx = *vpeg::context_data_t::current_ctx;

                static const auto shebang_q = v_quark_from_string("shebang");

                if (pctx.grammar.parsers.find(shebang_q))
                {
                    pctx.grammar.parse(shebang_q, pctx);

                    pctx.memo.clear();
                }
            }

            while(auto unit = parse_unit())
            {
                voidc_visitor_data_t::visit(lctx.compiler, unit);

                unit.reset();

                lctx.run_unit_action();

                LLVMDisposeMemoryBuffer(lctx.unit_buffer);

                lctx.unit_buffer = nullptr;
            }

            current_grammar = vpeg::context_data_t::current_ctx->grammar;

            vpeg::context_data_t::current_ctx = nullptr;     //- ?

            if (src != "-")   std::fclose(istr);
        }
    }

#if 0
    for (auto [f,d] : gctx.imported)
    {
        printf("\n%s\n", f.c_str());

        for (auto [s,t] : d.symbols)
        {
            printf("%s\n", s.c_str());
        }
    }
#endif

    vpeg::context_data_t::static_terminate();
    vpeg::grammar_data_t::static_terminate();
    voidc_visitor_data_t::static_terminate();
    v_ast_static_terminate();

    utility::static_terminate();
    voidc_global_ctx_t::static_terminate();

    return 0;
}


