/*
 * Heal is a lightweight C++ framework to aid and debug applications.
 * Copyright (c) 2011, 2012, 2013, 2014 Mario 'rlyeh' Rodriguez

 * Callstack code is based on code by Magnus Norddahl (See http://goo.gl/LM5JB)
 * Mem/CPU OS code is based on code by David Robert Nadeau (See http://goo.gl/8P5Jqv)

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.

 * - rlyeh // ~listening to Kalas - Monuments to Ruins
 */

// A few tweaks before loading STL on MSVC
// This improves stack unwinding.

#ifdef _SECURE_SCL
#undef _SECURE_SCL
#endif
#define _SECURE_SCL 0
#ifdef _HAS_ITERATOR_DEBUGGING
#undef _HAS_ITERATOR_DEBUGGING
#endif
#define _HAS_ITERATOR_DEBUGGING 0

// Standard headers

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// System headers

#ifdef _WIN32
//#   define UNICODE
//#   define _UNICODE
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <windows.h>
#   include <commctrl.h>
#   pragma comment(lib, "comctl32.lib")
#   if defined _M_IX86
#       pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#   elif defined _M_IA64
#       pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#   elif defined _M_X64
#       pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#   else
#       pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#   endif
#   // unwinding
#   if defined(DEBUG) || defined(_DEBUG)
#       include <CrtDbg.h>
#   endif
#   include <DbgHelp.h>
#   pragma comment(lib, "dbghelp.lib")
#   if defined(__GNUC__)
#       undef __GNUC__                         // switch MingW users here from $gnuc() to $windows()
#   endif
#   ifndef TD_SHIELD_ICON
#       define TD_SHIELD_ICON          MAKEINTRESOURCEW(-4)
#   endif
#else
#   include <unistd.h>
#   include <signal.h>
#   include <sys/time.h>
#   include <sys/types.h>
//  --
#   if defined(HAVE_SYS_SYSCTL_H) && \
        !defined(_SC_NPROCESSORS_ONLN) && !defined(_SC_NPROC_ONLN)
#       include <sys/param.h>
#       include <sys/sysctl.h>
#   endif
//  --
#   include <execinfo.h>
//  --
#   include <cxxabi.h>
#endif


// API

#include "heal.hpp"

// INFO MESSAGES
// Reminders for retrieving symbols

#  if $on($msvc)
    $warning( "<heal/heal.cpp> says: do not forget /Zi, /Z7 or /C7 compiler settings! /Oy- also helps!" )
#elif $on($clang)
    $warning( "<heal/heal.cpp> says: do not forget -g compiler setting!" )
#elif $on($gnuc)
    $warning( "<heal/heal.cpp> says: do not forget -g -lpthread compiler settings!" )
#endif

// ASSERT

std::vector< heal_callback_in > warns( {
    // default fallback
    []( const std::string &text ) {

        if( text.size() ) {
            alert( text, "Warning" );
        }

        return true;
} } );

std::vector< heal_callback_in > fails( {
    // default fallback
    []( const std::string &text ) {

        if( text.size() ) {
            errorbox( text, "Error" );
        }

        if( !debugger() ) {
            alert( "Could not launch debugger" );
        }

        return true;
} } );

std::vector< heal_callback_in > workers( {
    // default fallback
    []( const std::string &text ) {
        return true;
} } );

std::vector< heal_http_callback > servers( {
    // default fallback
    []( std::ostream &headers, std::ostream &content, const std::string &assertion ) {
        headers << "Content-Type: text/html;charset=UTF-8\r\n";
        content << "default fallback" << std::endl;
        return 200;
} } );


void warn( const std::string &error ) {
    static bool recursive = false;
    if( !recursive ) {
        recursive = true;
        for( unsigned i = warns.size(); i--; ) {
            if( warns[i] ) if( warns[i]( error ) ) break;
        }
        recursive = false;
    }
}

void fail( const std::string &error ) {
    static bool recursive = false;
    if( !recursive ) {
        recursive = true;
        for( unsigned i = fails.size(); i--; ) {
            if( fails[i] ) if( fails[i]( error ) ) break;
        }
        recursive = false;
    }
}

bool is_asserting()
{
    /*
    static bool enabled = ( enabled = false, assert( enabled ^= true ), enabled );
    return enabled;
    */
    /*
    static struct once { bool are_enabled; once() : are_enabled(false) {
        assert( are_enabled ^= true );
    } } asserts;
    return asserts.are_enabled;
    */
    bool asserting = false;
    assert( asserting |= true );
    return asserting;
}

// IS_DEBUG
// IS_RELEASE

bool is_debug() {
    $debug(
    return true;
    )
    $release(
    return false;
    )
}
bool is_release() {
    $release(
    return true;
    )
    $debug(
    return false;
    )
}

// DEBUGGER

#if $on($linux) || $on($apple)

    // enable core dumps for debug builds
    // after a crash try to do something like 'gdb ./a.out core'
#   if defined(NDEBUG) || defined(_NDEBUG)
        const bool are_coredumps_enabled = false;
#   else
#       include <sys/resource.h>
        rlimit core_limit = { RLIM_INFINITY, RLIM_INFINITY };
        const bool are_coredumps_enabled = setrlimit( RLIMIT_CORE, &core_limit ) == 0;
#   endif

    struct file {
        static bool exists( const std::string &pathfile) {
        /*struct stat buffer;
          return stat( pathfile.c_str(), &buffer ) == 0; */
          return access( pathfile.c_str(), F_OK ) != -1; // _access(fn,0) on win
        }
    };

    bool has( const std::string &app ) {
        return file::exists( std::string("/usr/bin/") + app );
    }

    std::string pipe( const std::string &sys, const std::string &sys2 = std::string() ) {
        char buf[512];
        std::string out;

        FILE *fp = popen( (sys+sys2).c_str(), "r" );
        if( fp ) {
            while( !std::feof(fp) ) {
                if( std::fgets(buf,sizeof(buf),fp) != NULL ) {
                    out += buf;
                }
            }
            pclose(fp);
        }

        return out;
    }

    // gdb apparently opens FD(s) 3,4,5 (whereas a typical prog uses only stdin=0, stdout=1,stderr=2)
    // Silviocesare and xorl
    bool detect_gdb(void)
    {
        bool rc = false;
        FILE *fd = fopen("/tmp", "r");

        if( fileno(fd) > 5 )
            rc = true;

        fclose(fd);
        return rc;
    }

#endif

void breakpoint() {
// os based

    $windows(
    DebugBreak();
    )

    $linux(
    raise(SIGTRAP);
//    asm("trap");
//    asm("int3");
//    kill( getpid(), SIGINT );
    /*
    kill( getpid(), SIGSTOP );
    kill( getpid(), SIGTERM );
    kill( getpid(), SIGHUP );
    kill( getpid(), SIGTRAP );
    */
    // kill( getpid(), SIGSEGV );
    // raise(SIGTRAP); //POSIX
    // raise(SIGINT);  //POSIX
    )

    $apple(
    raise(SIGTRAP);
    )

// compiler based

    //msvc
    $msvc(
    // "With native code, I prefer using int 3 vs DebugBreak, because the int 3 occurs right in the same stack frame and module as the code in question, whereas DebugBreak occurs one level down in another DLL, as shown in this callstack:"
    // [ref] http://blogs.msdn.com/b/calvin_hsia/archive/2006/08/25/724572.aspx
    // __debugbreak();
    )

    // gnuc
    $gnuc(
    //__builtin_trap();
    //__asm__ __volatile__("int3");
    )

// standard

    //abort();
    //assert( !"<heal/heal.cpp> says: debugger() has been requested" );
    // still here? duh, maybe we are in release mode...

// host based

    //macosx: asm {trap}            ; Halts a program running on PPC32 or PPC64.
    //macosx: __asm {int 3}         ; Halts a program running on IA-32.

    //$x86( // ifdef _M_X86
    //__asm int 3;
    //)
}


bool debugger( const std::string &reason )
{
    if( reason.size() > 0 )
        errorbox( reason );

// os based

    $windows(
        if( IsDebuggerPresent() )
            int(), true;
    )

    $linux(
        if( detect_gdb() ) {
            int(), true;
        }
        // else try to invoke && attach to current process

        static std::string sys, tmpfile;
        sys = ( has("ddd") && false ? "/usr/bin/ddd" : ( has("gdb") ? "/usr/bin/gdb" : "" ));
        tmpfile = "./heal.tmp.tmp"; //get_pipe("tempfile");
        if( !sys.empty() ) {
            std::string pid = std::to_string( getpid() );
            // [ok]
            // eval-command=bt
            // -ex "bt full"
            // gdb --batch --quiet -ex "thread apply all bt full" -ex "quit" ${exe} ${corefile}
            sys = sys + (" --tui -q -ex 'set pagination off' -ex 'shell rm " +tmpfile+ "' -ex 'continue' -ex 'finish' -ex 'finish' -ex 'finish' --pid=") + pid + " --args `cat /proc/" + pid + "/cmdline`";
            if( has("xterm") && false ) {
                sys = std::string("/usr/bin/xterm 2>/dev/null -maximized -e \"") + sys + "\"";
            } else {
                //sys = std::string(/*"exec"*/ "/usr/bin/splitvt -upper \"") + sys + "\"";
                //sys = std::string("/bin/bash -c \"") + sys + " && /usr/bin/reset\"";
                sys = std::string("/bin/bash -c \"") + sys + "\"";
            }

        pipe( "echo heal.cpp says: waiting for debugger to catch pid > ", tmpfile );
        std::thread( system, sys.c_str() ).detach();
        while( file::exists(tmpfile) )
            usleep( 250000 );
                return true;
        }
    )

    //errorbox( "<heal/heal.cpp> says:\n\nDebugger invokation failed.\nPlease attach a debugger now.", "Error!");
    return false;
}

// ERRORBOX

namespace {

    template<typename T>
    std::string to_string( const T &t, int digits = 20 ) {
        std::stringstream ss;
        ss.precision( digits );
        ss << std::fixed << t;
        return ss.str();
    }

    template<>
    std::string to_string( const bool &boolean, int digits ) {
        return boolean ? "true" : "false";
    }

    template<>
    std::string to_string( const std::istream &is, int digits ) {
        std::stringstream ss;
        std::streamsize at = is.rdbuf()->pubseekoff(0,is.cur);
        ss << is.rdbuf();
        is.rdbuf()->pubseekpos(at);
        return ss.str();
    }

    void show( const std::string &body = std::string(), const std::string &head = std::string(), const std::string &title = std::string(), bool is_error = false ) {
        std::string headtitle = ( head.size() > 0 ? head + ": " + title : title );
        std::string headtitlebody = ( headtitle.size() > 0 ? headtitle + ": " + body : body );
        $windows(
            $no(
            int nButton;
            auto icon = is_error ? TD_ERROR_ICON : TD_WARNING_ICON;
            // TD_INFORMATION_ICON, TD_SHIELD_ICON
            std::wstring wbody( body.begin(), body.end() );
            std::wstring whead( head.begin(), head.end() );
            std::wstring wtitle( title.begin(), title.end() );
            HWND hWnd = ::GetActiveWindow(); // force modal
            TaskDialog(hWnd, NULL, wtitle.c_str(), whead.c_str(), wbody.c_str(), TDCBF_OK_BUTTON, icon, &nButton );
            )
            $yes(
            MessageBoxA( 0, body.c_str(), head.size() ? head.c_str() : "", 0 | ( is_error ? MB_ICONERROR : 0 ) | MB_SYSTEMMODAL );
            )
            return;
        )
        $linux(
            if( has("whiptail") ) {
                // gtkdialog3
                // xmessage -file ~/.bashrc -buttons "Ok:1, Cancel:2, Help:3" -print -nearmouse
                //std::string cmd = std::string("/usr/bin/zenity --information --text \"") + body + std::string("\" --title=\"") + headtitle + "\"";
                //std::string cmd = std::string("/usr/bin/dialog --title \"") + headtitle + std::string("\" --msgbox \"") + body + "\" 0 0";
                std::string cmd = std::string("/usr/bin/whiptail --title \"") + headtitle + std::string("\" --msgbox \"") + body + "\" 0 0";
                //std::string cmd = std::string("/usr/bin/xmessage \"") + headtitle + body + "\"";
                std::system( cmd.c_str() );
                return;
            }
        )
        // fallback
        std::string s;
        fprintf( stderr, "%s\n", headtitlebody.c_str() );
        std::cout << "Press enter to continue..." << std::endl;
        std::getline( std::cin, s );
    }
}

void    alert(                                                    ) { show();                        }
void    alert( const         char *text, const std::string &title ) { show( text, title );           }
void    alert( const  std::string &text, const std::string &title ) { show( text, title );           }
void    alert( const std::istream &text, const std::string &title ) { show( to_string(text), title ); }
void    alert( const       size_t &text, const std::string &title ) { show( to_string(text), title ); }
void    alert( const       double &text, const std::string &title ) { show( to_string(text), title ); }
void    alert( const        float &text, const std::string &title ) { show( to_string(text), title ); }
void    alert( const          int &text, const std::string &title ) { show( to_string(text), title ); }
void    alert( const         char &text, const std::string &title ) { show( to_string(text), title ); }
void    alert( const         bool &text, const std::string &title ) { show( to_string(text), title ); }
void errorbox( const  std::string &body, const std::string &title ) { show( body, title, "", true );  }

// DEMANGLE

#if 1
#   // MSVC tweaks
#   // Disable optimizations. Nothing gets inlined. You get pleasant stacktraces to work with.
#   // This is the default setting.
#   ifdef _MSC_VER
#       pragma optimize( "gsy", off )       // disable optimizations on msvc
#   else
#       pragma OPTIMIZE OFF                 // disable optimizations on gcc 4.4+
#   endif
#else
#   // Enable optimizations. HEAL performs better. However, functions get inlined (specially new/delete operators).
#   // This behaviour is disabled by default, since you get weird stacktraces.
#   ifdef _MSC_VER
#       pragma optimize( "gsy", on )        // enable optimizations on msvc
#   else
#       pragma GCC optimize                 // enable optimizations on gcc 4.4+
#       pragma optimize                     // enable optimizations on a few other compilers, hopefully
#   endif
#endif

namespace heal
{
    class string : public std::string
    {
        public:

        string() : std::string()
        {}

        template <typename T>
        /* explicit */ string( const T &t ) : std::string()
        {
            std::stringstream ss;
            if( ss << t )
                this->assign( ss.str() );
        }

        template <typename T1, typename T2 = std::string, typename T3 = std::string>
        explicit string( const char *fmt123, const T1 &_t1, const T2 &_t2 = T2(), const T3 &_t3 = T3() ) : std::string()
        {
            string t1( _t1 );
            string t2( _t2 );
            string t3( _t3 );

            string &s = *this;

            while( *fmt123 )
            {
                if( *fmt123 == '\1' )
                    s += t1;
                else
                if( *fmt123 == '\2' )
                    s += t2;
                else
                if( *fmt123 == '\3' )
                    s += t3;
                else
                    s += *fmt123;

                fmt123++;
            }
        }

        size_t count( const std::string &substr ) const
        {
            size_t n = 0;
            std::string::size_type pos = 0;
            while( (pos = this->find( substr, pos )) != std::string::npos ) {
                n++;
                pos += substr.size();
            }
            return n;
        }

        std::string replace( const std::string &target, const std::string &replacement ) const {
            size_t found = 0;
            std::string s = *this;

            while( ( found = s.find( target, found ) ) != string::npos ) {
                s.replace( found, target.length(), replacement );
                found += replacement.length();
            }

            return s;
        }
    };
}

std::string demangle( const std::string &mangled ) {
    $linux({
        $no( /* c++filt way */
        FILE *fp = popen( (std::string("echo -n \"") + mangled + std::string("\" | c++filt" )).c_str(), "r" );
        if (!fp) { return mangled; }
        char demangled[1024];
        char *line_p = fgets(demangled, sizeof(demangled), fp);
        pclose(fp);
        return demangled;
        )
        $yes( /* addr2line way. wip & quick proof-of-concept. clean up required. */
        heal::string binary = mangled.substr( 0, mangled.find_first_of('(') );
        heal::string address = mangled.substr( mangled.find_last_of('[') + 1 );
        address.pop_back();
        heal::string cmd( "addr2line -e \1 \2", binary, address );
        FILE *fp = popen( cmd.c_str(), "r" );
        if (!fp) { return mangled; }
        char demangled[1024];
        char *line_p = fgets(demangled, sizeof(demangled), fp);
        pclose(fp);
        heal::string demangled_(demangled);
        if( demangled_.size() ) demangled_.pop_back(); //remove \n
        return demangled_.size() && demangled_.at(0) == '?' ? mangled : demangled_;
        )
    })
    $apple({
        std::stringstream ss;
        if( !(ss << mangled) )
            return mangled;
        std::string number, filename, address, funcname, plus, offset;
        if( !(ss >> number >> filename >> address >> funcname >> plus >> offset) )
            return mangled;
        int status = 0;
        char *demangled = abi::__cxa_demangle(funcname.c_str(), NULL, NULL, &status);
        heal::string out( mangled );
        if( status == 0 && demangled )
            out = out.replace( funcname, demangled );
        if( demangled ) free( demangled );
        return out;
    })
    $windows({
        char demangled[1024];
        return (UnDecorateSymbolName(mangled.c_str(), demangled, sizeof( demangled ), UNDNAME_COMPLETE)) ? std::string(demangled) : mangled;
    })
    $gnuc({
        std::string out;
        int status = 0;
        char *demangled = abi::__cxa_demangle(mangled.c_str(), 0, 0, &status);
        out = ( status == 0 && demangled ? std::string(demangled) : mangled );
        if( demangled ) free( demangled );
        return out;
    })
        return mangled;
}

// CALLSTACK


        callstack::callstack( bool autosave ) {
            if( autosave ) save();
        }

        size_t callstack::space() const {
            return sizeof(frames) + sizeof(void *) * frames.size();
        }

        void callstack::save( unsigned frames_to_skip ) {

            if( frames_to_skip > max_frames )
                return;

            frames.clear();
            frames.resize( max_frames, (void *)0 );
            void **out_frames = &frames[0]; // .data();

            $windows({
                unsigned short capturedFrames = 0;

                // RtlCaptureStackBackTrace is only available on Windows XP or newer versions of Windows
                typedef WORD(NTAPI FuncRtlCaptureStackBackTrace)(DWORD, DWORD, PVOID *, PDWORD);

                static struct raii
                {
                    raii() : module(0), ptrRtlCaptureStackBackTrace(0)
                    {
                        module = LoadLibraryA("kernel32.dll");
                        if( !module )
                            fail( "<heal/heal.cpp> says: error! cant load kernel32.dll" );

                        ptrRtlCaptureStackBackTrace = (FuncRtlCaptureStackBackTrace *)GetProcAddress(module, "RtlCaptureStackBackTrace");
                        if( !ptrRtlCaptureStackBackTrace )
                            fail( "<heal/heal.cpp> says: error! cant find RtlCaptureStackBackTrace() process address" );
                    }
                    ~raii() { if(module) FreeLibrary(module); }

                    HMODULE module;
                    FuncRtlCaptureStackBackTrace *ptrRtlCaptureStackBackTrace;
                } module;

                if( module.ptrRtlCaptureStackBackTrace )
                    capturedFrames = module.ptrRtlCaptureStackBackTrace(frames_to_skip+1, max_frames, out_frames, (DWORD *) 0);

                frames.resize( capturedFrames );
                std::vector<void *>(frames).swap(frames);
                return;
            })
            $gnuc({
                // Ensure the output is cleared
                std::memset(out_frames, 0, (sizeof(void *)) * max_frames);

                frames.resize( backtrace(out_frames, max_frames) );
                std::vector<void *>(frames).swap(frames);
                return;
            })
        }

        std::vector<std::string> callstack::unwind( unsigned from, unsigned to ) const
        {
            if( to == ~0 )
                to = this->frames.size();

            if( from > to || from > this->frames.size() || to > this->frames.size() )
                return std::vector<std::string>();

            const size_t num_frames = to - from;
            std::vector<std::string> backtraces( num_frames );

            void * const * frames = &this->frames[ from ];
            const std::string invalid = "????";

            $windows({
                SymSetOptions(SYMOPT_UNDNAME);

                $no(
                    // polite version. this is how things should be done.
                    HANDLE process = GetCurrentProcess();
                    if( SymInitialize( process, NULL, TRUE ) )
                )
                $yes(
                    // this is what we have to do because other memory managers are not polite enough. fuck them off
                    static HANDLE process = GetCurrentProcess();
                    static int init = SymInitialize( process, NULL, TRUE );
                    if( !init )
                        fail( "<heal/heal.cpp> says: cannot initialize Dbghelp.lib" );
                )
                {
                    enum { MAXSYMBOLNAME = 512 - sizeof(IMAGEHLP_SYMBOL64) };
                    char symbol64_buf     [ 512 ];
                    char symbol64_bufblank[ 512 ] = {0};
                    IMAGEHLP_SYMBOL64 *symbol64       = reinterpret_cast<IMAGEHLP_SYMBOL64*>(symbol64_buf);
                    IMAGEHLP_SYMBOL64 *symbol64_blank = reinterpret_cast<IMAGEHLP_SYMBOL64*>(symbol64_bufblank);
                    symbol64_blank->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
                    symbol64_blank->MaxNameLength = (MAXSYMBOLNAME-1) / 2; //wchar?

                    IMAGEHLP_LINE64 line64, line64_blank = {0};
                    line64_blank.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

                    for( unsigned i = 0; i < num_frames; i++ ) {
                        *symbol64 = *symbol64_blank;
                        DWORD64 displacement64 = 0;

                        if( SymGetSymFromAddr64( process, (DWORD64) frames[i], &displacement64, symbol64 ) ) {
                            line64 = line64_blank;
                            DWORD displacement = 0;
                            if( SymGetLineFromAddr64( process, (DWORD64) frames[i], &displacement, &line64 ) ) {
                                backtraces[i] = heal::string( "\1 (\2, line \3)", symbol64->Name, line64.FileName, line64.LineNumber );
                            } else {
                                backtraces[i] = symbol64->Name;
                            }
                        } else  backtraces[i] = invalid;
                    }

                    $no(
                        // fuck the others. cleanup commented.
                        SymCleanup(process);
                    )
                }
                DWORD error = GetLastError();

                return backtraces;
            })
            $gnuc({
                char **strings = backtrace_symbols(frames, num_frames);

                // Decode the strings
                if( strings ) {
                    for( unsigned i = 0; i < num_frames; i++ ) {
                        backtraces[i] = ( strings[i] ? demangle(strings[i]) : invalid );
                    }
                    free( strings );
                }

                return backtraces;
            })

            return backtraces;
        }

        std::vector<std::string> callstack::str( const char *format12, size_t skip_begin ) {
            std::vector<std::string> stacktrace = unwind( skip_begin );

            for( size_t i = 0, end = stacktrace.size(); i < end; i++ )
                stacktrace[i] = heal::string( format12, i + 1, stacktrace[i] );

            return stacktrace;
        }

std::vector<std::string> stacktrace( const char *format12, size_t skip_initial ) {
    return callstack(true).str( format12, skip_initial );
}

std::string stackstring( const char *format12, size_t skip_initial ) {
    std::string out;
    auto stack = stacktrace( format12, skip_initial );
    for( auto &line : stack ) {
        out += line;
    }
    return out;
}

// DIE

void die( const std::string &reason, int errorcode )
{
    if( !reason.empty() ) {
        fail( reason );
    }

    $windows(
    FatalExit( errorcode );
    )

    // fallback
    std::exit( errorcode );
}

void die( int errorcode, const std::string &reason )
{
    die( reason, errorcode );
}

// HEXDUMP
// @todo: maxwidth != 80 doesnt work

std::string hexdump( const void *data, size_t num_bytes, const void *self )
{
#   ifdef _MSC_VER
#       pragma warning( push )
#       pragma warning( disable : 4996 )
#       define $vsnprintf _vsnprintf
#   else
#       define $vsnprintf  vsnprintf
#   endif

    struct local {
        static std::string format( const char *fmt, ... )
        {
            int len;
            std::string self;

            using namespace std;

            // Calculate the final length of the formatted string
            {
                va_list args;
                va_start( args, fmt );
                len = $vsnprintf( 0, 0, fmt, args );
                va_end( args );
            }

            // Allocate a buffer (including room for null termination
            char* target_string = new char[++len];

            // Generate the formatted string
            {
                va_list args;
                va_start( args, fmt );
                $vsnprintf( target_string, len, fmt, args );
                va_end( args );
            }

            // Assign the formatted string
            self.assign( target_string );

            // Clean up
            delete [] target_string;

            return self;
        }
    };

	using std::max;
    unsigned maxwidth = 80;
    unsigned width = 16; //column width
    unsigned width_offset_block = (8 + 1);
    unsigned width_chars_block  = (width * 3 + 1) + sizeof("asc");
    unsigned width_hex_block    = (width * 3 + 1) + sizeof("hex");
    unsigned width_padding = max( 0, int( maxwidth - ( width_offset_block + width_chars_block + width_hex_block ) ) );
	unsigned blocks = width_padding / ( width_chars_block + width_hex_block ) ;

    unsigned dumpsize = ( num_bytes < width * 16 ? num_bytes : width * 16 ); //16 lines max

    std::string result;

    result += local::format( "%-*s %-.*s [ptr=%p sz=%d]\n", width_offset_block - 1, "offset", width_chars_block - 1, "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F", self ? self : data, num_bytes );

    if( !num_bytes )
        return result;

    blocks++;

    const unsigned char *p = reinterpret_cast<const unsigned char *>( data );
    size_t i = 0;

    while( i < dumpsize )
    {
        //offset block
        result += local::format("%p ", (size_t)(p + i) ); //%08x, %08zx

        //chars blocks
        for( unsigned b = 0; b < blocks; b++)
        {
            for( unsigned c = 0 ; c < width ; c++ )
                result += local::format(" %c ", i + c >= dumpsize ? '.' : p[i + c] < 32 || p[i + c] >= 127 ? '.' : p[i + c]);

            result += "asc\n";
        }

        //offset block
        result += local::format("%p ", (size_t)(p + i) ); //%08x, %08zx

        //hex blocks
        for( unsigned b = 0; b < blocks; b++)
        {
            for( unsigned c = 0; c < width ; c++)
                result += local::format( i + c < dumpsize ? "%02x " : "?? ", p[i + c]);

            result += "hex\n";
        }

        //next line
        //result += '\n';
        i += width * blocks;
    }

    return result;

#   undef $vsnprintf
#   ifdef _MSC_VER
#       pragma warning( pop )
#   endif
}

std::string timestamp() {
    std::stringstream ss;
    ss << __TIMESTAMP__;
    return ss.str();
}



/*
 * Simple prompt dialog (based on legolas558's code). MIT license.
 * - rlyeh
 */

#if !$on($windows)

std::string prompt( const std::string &title, const std::string &current_value, const std::string &caption )
{
    std::string out;

    if( has("whiptail") && false )
    {
        std::string out = pipe( std::string() +
            "/usr/bin/whiptail 3>&1 1>&2 2>&3 --title \"" +title+ "\" --inputbox \"" +caption+ "\" 0 0 \"" +current_value+ "\"" );
    }
    else
    {
        if( title.size() && caption.size() )
            fprintf(stdout, "%s", title.c_str());
        else
            fprintf(stdout, "%s", title.empty() ? caption.c_str() : title.c_str());

        if( !current_value.empty() )
            fprintf(stdout, " (enter defaults to '%s')", current_value.c_str());

        fprintf(stdout, "%s", "\n");

        std::getline( std::cin, out );

        if( out.empty() )
            out = current_value;
    }

    return out;
}

#endif




#if $on($msvc)
#   pragma warning( push )
#   pragma warning( disable : 4996 )
#endif

#if $on($windows)
#   pragma comment(lib,"user32.lib")
#   pragma comment(lib,"gdi32.lib")
$warning("<heal/heal.cpp> says: dialog aware dpi fix (@todo)")

std::string prompt( const std::string &title, const std::string &current_value, const std::string &caption )
{
    class InputBox
    {
        private:

        HWND                hwndParent,
                            hwndInputBox,
                            hwndQuery,
                            hwndOk,
                            hwndCancel,
                            hwndEditBox;
        LPSTR               szInputText;
        WORD                wInputMaxLength, wInputLength;
        bool                bRegistered,
                            bResult;

        HINSTANCE           hThisInstance;

        enum
        {
            CIB_SPAN = 10,
            CIB_LEFT_OFFSET = 6,
            CIB_TOP_OFFSET = 4,
            CIB_WIDTH = 300,
            CIB_HEIGHT = 130,
            CIB_BTN_WIDTH = 60,
            CIB_BTN_HEIGHT = 20
        };

        public:

#       define CIB_CLASS_NAME   "CInputBoxA"

        static LRESULT CALLBACK CIB_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            InputBox *self;
            self = (InputBox *)GetWindowLong(hWnd, GWL_USERDATA);

            switch (msg)
            {
                case WM_CREATE:
                    self = (InputBox *) ((CREATESTRUCT *)lParam)->lpCreateParams;
                    SetWindowLong(hWnd, GWL_USERDATA, (long)self);
                    self->create(hWnd);
                break;
                case WM_COMMAND:
                    switch(LOWORD(wParam)) {
                        case IDOK:
                            self->submit();
                        case IDCANCEL:
                            self->close();
                        break;
                    }
                    break;
                case WM_CLOSE:
                    self->hide();
                    return 0;
                case WM_DESTROY:
                    self->destroy();
                    break;
            }
            return(DefWindowProc (hWnd, msg, wParam, lParam));
        }

        InputBox( HINSTANCE hInst ) :
            hwndParent(0),
            hwndInputBox(0),
            hwndQuery(0),
            hwndOk(0),
            hwndCancel(0),
            hwndEditBox(0),
            szInputText(""),
            wInputMaxLength(0), wInputLength(0),
            bRegistered(false),
            bResult(false),
            hThisInstance(0)
        {
            WNDCLASSEXA wndInputBox;
            RECT rect;

            memset(&wndInputBox, 0, sizeof(WNDCLASSEXA));

            hThisInstance = hInst;

            wndInputBox.cbSize                  = sizeof(wndInputBox);
            wndInputBox.lpszClassName           = CIB_CLASS_NAME;
            wndInputBox.style                   = CS_HREDRAW | CS_VREDRAW;
            wndInputBox.lpfnWndProc             = CIB_WndProc;
            wndInputBox.lpszMenuName            = NULL;
            wndInputBox.hIconSm                 = NULL;
            wndInputBox.cbClsExtra              = 0;
            wndInputBox.cbWndExtra              = 0;
            wndInputBox.hInstance               = hInst;
            wndInputBox.hIcon                   = LoadIcon(NULL, IDI_WINLOGO);
            wndInputBox.hCursor                 = LoadCursor(NULL, IDC_ARROW);
            wndInputBox.hbrBackground           = (HBRUSH)(COLOR_WINDOW);

            RegisterClassExA(&wndInputBox);

            if (hwndParent)
                GetWindowRect(hwndParent, &rect); //always false?
            else
                GetWindowRect(GetDesktopWindow(), &rect);

            hwndInputBox = CreateWindowA( CIB_CLASS_NAME, "",
                            (WS_BORDER | WS_CAPTION), rect.left+(rect.right-rect.left-CIB_WIDTH)/2,
                            rect.top+(rect.bottom-rect.top-CIB_HEIGHT)/2,
                            CIB_WIDTH, CIB_HEIGHT, hwndParent, NULL,
                            hThisInstance, this);
        }

        void destroy()
        {
            EnableWindow(hwndParent, true);
            SendMessage(hwndInputBox, WM_CLOSE/*WM_DESTROY*/, 0, 0);
        }

        ~InputBox()
        {
            UnregisterClassA(CIB_CLASS_NAME, hThisInstance);
        }

        void submit()
        {
            wInputLength = (int)SendMessage(hwndEditBox, EM_LINELENGTH, 0, 0);
            if (wInputLength) {
                *((LPWORD)szInputText) = wInputMaxLength;
                wInputLength = (WORD)SendMessage(hwndEditBox, EM_GETLINE, 0, (LPARAM)szInputText);
            }
            szInputText[wInputLength] = '\0';
            bResult = true;
        }

        void create(HWND hwndNew)
        {
            static HFONT myFont = NULL;

            if( myFont != NULL )
            {
                DeleteObject( myFont );
                myFont = NULL;
            }

            hwndInputBox = hwndNew;

            NONCLIENTMETRICS ncm;
            ncm.cbSize = sizeof(NONCLIENTMETRICS) - sizeof(ncm.iPaddedBorderWidth);

            if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
            {
#if 0
                LOGFONT lf;
                memset(&lf,0,sizeof(LOGFONT));

                lf.lfWeight= FW_NORMAL;
                lf.lfCharSet= ANSI_CHARSET;
                //lf.lfPitchAndFamily = 35;
                lf.lfHeight= 10;
                strcpy(lf.lfFaceName, "Tahoma");
                myFont=CreateFontIndirect(&lf);
#else
                myFont = CreateFontIndirect(&ncm.lfMessageFont);
#endif
            }
            else
            {
                myFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            }

            //  SetWindowPos(hwndInputBox, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            hwndQuery = CreateWindowA("Static", "", WS_CHILD | WS_VISIBLE,
                                    CIB_LEFT_OFFSET, CIB_TOP_OFFSET,
                                    CIB_WIDTH-CIB_LEFT_OFFSET*2, CIB_BTN_HEIGHT*2,
                                    hwndInputBox, NULL,
                                    hThisInstance, NULL);
            hwndEditBox = CreateWindowA("Edit", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_LEFT, CIB_LEFT_OFFSET,
                                    CIB_TOP_OFFSET + CIB_BTN_HEIGHT*2, CIB_WIDTH-CIB_LEFT_OFFSET*3, CIB_BTN_HEIGHT,
                                    hwndInputBox,   NULL,
                                    hThisInstance, NULL);
            hwndOk = CreateWindowA("Button", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    CIB_WIDTH/2 - CIB_SPAN*2 - CIB_BTN_WIDTH, CIB_HEIGHT - CIB_TOP_OFFSET*4 - CIB_BTN_HEIGHT*2,
                                    CIB_BTN_WIDTH, CIB_BTN_HEIGHT, hwndInputBox, (HMENU)IDOK,
                                    hThisInstance, NULL);
            hwndCancel = CreateWindowA("Button", "Cancel",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    CIB_WIDTH/2 + CIB_SPAN, CIB_HEIGHT - CIB_TOP_OFFSET*4 - CIB_BTN_HEIGHT*2,  CIB_BTN_WIDTH, CIB_BTN_HEIGHT,
                                    hwndInputBox, (HMENU)IDCANCEL,
                                    hThisInstance, NULL);

        //  SendMessage(hwndInputBox,WM_SETFONT,(WPARAM)myFont,FALSE);
            SendMessage(hwndQuery,WM_SETFONT,(WPARAM)myFont,FALSE);
            SendMessage(hwndEditBox,WM_SETFONT,(WPARAM)myFont,FALSE);
            SendMessage(hwndOk,WM_SETFONT,(WPARAM)myFont,FALSE);
            SendMessage(hwndCancel,WM_SETFONT,(WPARAM)myFont,FALSE);
        }

        void close()
        {
            PostMessage(hwndInputBox, WM_CLOSE, 0, 0);
        }

        void hide()
        {
            ShowWindow(hwndInputBox, SW_HIDE);
        }

        void show(LPCSTR lpszTitle, LPCSTR  lpszQuery)
        {
            SetWindowTextA(hwndInputBox, lpszTitle);
            SetWindowTextA(hwndEditBox, szInputText);
            SetWindowTextA(hwndQuery, lpszQuery);
            SendMessage(hwndEditBox, EM_LIMITTEXT, wInputMaxLength, 0);
            SendMessage(hwndEditBox, EM_SETSEL, 0, -1);
            SetFocus(hwndEditBox);
            ShowWindow(hwndInputBox, SW_NORMAL);
        }

        int show(HWND hwndParentWindow, LPCSTR lpszTitle, LPCSTR lpszQuery, LPSTR szResult, WORD wMax)
        {
            MSG msg;
            BOOL    bRet;
            hwndParent = hwndParentWindow;
            szInputText = szResult;
            wInputMaxLength = wMax;

            bResult = false;

        //  EnableWindow(hwndParent, false);

            show(lpszTitle, lpszQuery);

            while( (bRet = GetMessageA( &msg, NULL, 0, 0 )) != 0)
            {
                if (msg.message==WM_KEYDOWN) {
                    switch (msg.wParam) {
                    case VK_RETURN:
                        submit();
                    case VK_ESCAPE:
                        close();
                        break;
                    default:
                        TranslateMessage(&msg);
                        break;
                    }
                } else
        //      if (!IsDialogMessage(hwndInputBox, &msg)) {
                    TranslateMessage(&msg);
        //      }
                DispatchMessage(&msg);
                if (msg.message == WM_CLOSE)
                    break;
            }

        //  EnableWindow(hwndParent, true);

            return bResult;
        }

    #   undef CIB_CLASS_NAME
    }
    myinp(GetModuleHandle(0));

    char *result = new char [2048+1];

    memset( result, 0, 2048+1 ); //default value

    strcpy( result, current_value.c_str() );

    myinp.show(0, "L", caption.size() ? caption.c_str() : title.c_str(), result, 2048);

    std::string _r = result;

    delete [] result;

    return _r;
}

#endif






// Sockets

#if $on($windows)

#   include <winsock2.h>
#   include <ws2tcpip.h>
#   include <windows.h>

#   pragma comment(lib,"ws2_32.lib")

#   define INIT()                    do { static WSADATA wsa_data; static const int init = WSAStartup( MAKEWORD(2, 2), &wsa_data ); } while(0)
#   define SOCKET(A,B,C)             ::socket((A),(B),(C))
#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::closesocket((A))
#   define RECV(A,B,C,D)             ::recv((A), (char *)(B), (C), (D))
#   define READ(A,B,C)               ::recv((A), (char *)(B), (C), (0))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const char *)(B), (int)(C), (D))
#   define WRITE(A,B,C)              ::send((A), (const char *)(B), (int)(C), (0))
#   define GETSOCKOPT(A,B,C,D,E)     ::getsockopt((A),(B),(C),(char *)(D), (int*)(E))
#   define SETSOCKOPT(A,B,C,D,E)     ::setsockopt((A),(B),(C),(char *)(D), (int )(E))

#   define BIND(A,B,C)               ::bind((A),(B),(C))
#   define LISTEN(A,B)               ::listen((A),(B))
#   define SHUTDOWN(A)               ::shutdown((A),2)
#   define SHUTDOWN_R(A)             ::shutdown((A),0)
#   define SHUTDOWN_W(A)             ::shutdown((A),1)

    namespace
    {
        // fill missing api

        enum
        {
            F_GETFL = 0,
            F_SETFL = 1,

            O_NONBLOCK = 128 // dummy
        };

        int fcntl( int &sockfd, int mode, int value )
        {
            if( mode == F_GETFL ) // get socket status flags
                return 0; // original return current sockfd flags

            if( mode == F_SETFL ) // set socket status flags
            {
                u_long iMode = ( value & O_NONBLOCK ? 0 : 1 );

                bool result = ( ioctlsocket( sockfd, FIONBIO, &iMode ) == NO_ERROR );

                return 0;
            }

            return 0;
        }
    }

#else

#   include <fcntl.h>
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <netdb.h>
#   include <unistd.h>    //close

#   include <arpa/inet.h> //inet_addr

#   define INIT()                    do {} while(0)
#   define SOCKET(A,B,C)             ::socket((A),(B),(C))
#   define ACCEPT(A,B,C)             ::accept((A),(B),(C))
#   define CONNECT(A,B,C)            ::connect((A),(B),(C))
#   define CLOSE(A)                  ::close((A))
#   define READ(A,B,C)               ::read((A),(B),(C))
#   define RECV(A,B,C,D)             ::recv((A), (void *)(B), (C), (D))
#   define SELECT(A,B,C,D,E)         ::select((A),(B),(C),(D),(E))
#   define SEND(A,B,C,D)             ::send((A), (const int8 *)(B), (C), (D))
#   define WRITE(A,B,C)              ::write((A),(B),(C))
#   define GETSOCKOPT(A,B,C,D,E)     ::getsockopt((int)(A),(int)(B),(int)(C),(      void *)(D),(socklen_t *)(E))
#   define SETSOCKOPT(A,B,C,D,E)     ::setsockopt((int)(A),(int)(B),(int)(C),(const void *)(D),(int)(E))

#   define BIND(A,B,C)               ::bind((A),(B),(C))
#   define LISTEN(A,B)               ::listen((A),(B))
#   define SHUTDOWN(A)               ::shutdown((A),SHUT_RDWR)
#   define SHUTDOWN_R(A)             ::shutdown((A),SHUT_RD)
#   define SHUTDOWN_W(A)             ::shutdown((A),SHUT_WR)

#endif

void add_worker( heal_callback_in fn ) {
    static struct once { once() {
        volatile bool installed = false;
        std::thread( [&]() {
            installed = true;
            for(;;) {
                for( auto &wk : workers ) {
                    wk( std::string() );
                }
                $windows( Sleep( 1000/60 ) );
                $welse( usleep( 1000000/60 ) );
            }
        } ).detach();
        while( !installed ) {
            $windows( Sleep( 1000 ) );
            $welse( usleep( 1000000 ) );
        }
    }} _;

    workers.push_back( fn );
}

void add_webmain( int port, heal_http_callback fn ) {

//  add_firewall_rule( "webmain (HEAL library)", true, true, port );

    std::thread( [=]() {
        INIT();
        int s = SOCKET(PF_INET, SOCK_STREAM, IPPROTO_IP);

        {
            struct sockaddr_in l;
            memset( &l, 0, sizeof(sockaddr_in) );
            l.sin_family = AF_INET;
            l.sin_port = htons(port);
            l.sin_addr.s_addr = INADDR_ANY;
            $welse(
                int r = 1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&r,sizeof(r));
            )
            BIND(s,(struct sockaddr *)&l,sizeof(l));
            LISTEN(s,5);
        }
        for( ;; ) {

            int c = ACCEPT(s,0,0), o = 0, h[2], hi = 0;
            char b[1024];
            while(hi<2&&o<1024) {
                int n = READ(c,b+o,sizeof(b)-o);
                if(n<=0) { break; }
                else {
                    int i = o;
                    o+=n;
                    for(;i<n&&hi<2;i++) {
                        if(b[i] == '/' || (hi==1&&b[i] == ' ')) { h[hi++] = i; }
                    }
                }
            }
            if(hi == 2) {
                b[ o ] = '\0';

                char org = b[h[1]];
                b[h[1]] = '\0';
                std::string url( &b[4] ); //skip "GET "
                b[h[1]] = org;

                std::stringstream headers, content;
                int httpcode = fn( headers, content, url );

                if( httpcode > 0 ) {
                    std::string head = std::string() + "HTTP/1.1 " + to_string(httpcode) + " OK\r\n";
                    std::string header = headers.str();
                    std::string response = content.str();

                    header += "Content-Length: " + to_string( response.size() ) + "\r\n" +
                              "\r\n";

                    WRITE( c, head.c_str(), head.size() );
                    WRITE( c, header.c_str(), header.size() );
                    WRITE( c, response.c_str(), response.size() );
                } else {
                    WRITE( c, "{}", 2 );
                }

                SHUTDOWN(c);
                CLOSE(c);
            }
        }
    } ).detach();
}

#if $on($msvc)
#   pragma warning( pop )
#endif

/*
#undef $debug
#undef $release
#undef $other
#undef $gnuc
#undef $msvc

#undef $undefined
#undef $apple
#undef $linux
#undef $windows

#undef $no
#undef $yes
*/






#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "Kernel32.lib")
#pragma comment(lib, "Psapi.lib")

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/resource.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
#include <fcntl.h>
#include <procfs.h>

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
#include <stdio.h>

#endif

#else
#error "Cannot define get_mem_peak() or get_mem_current() for an unknown OS."
#endif



#if defined(_WIN32)
#include <Windows.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#if defined(BSD)
#include <sys/sysctl.h>
#endif

#else
#error "Unable to define get_mem_size() for an unknown OS."
#endif



#if defined(_WIN32)
#include <Windows.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <time.h>

#else
#error "Unable to define get_time_thread() for an unknown OS."
#endif





#if defined(_WIN32)
#include <Windows.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h> /* POSIX flags */
#include <time.h>   /* clock_gettime(), time() */
#include <sys/time.h>   /* gethrtime(), gettimeofday() */

#if defined(__MACH__) && defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#else
#error "Unable to define get_time_os() for an unknown OS."
#endif



/**
 * Returns the peak (maximum so far) resident set size (physical
 * memory use) measured in bytes, or zero if the value cannot be
 * determined on this OS.
 */
size_t get_mem_peak( )
{
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS info;
    info.cb = sizeof(info);
    GetProcessMemoryInfo( GetCurrentProcess( ), &info, sizeof(info) );
    return (size_t)info.PeakWorkingSetSize;

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
    /* AIX and Solaris ------------------------------------------ */
    struct psinfo psinfo;
    int fd = -1;
    if ( (fd = open( "/proc/self/psinfo", O_RDONLY )) == -1 )
        return (size_t)0L;      /* Can't open? */
    if ( read( fd, &psinfo, sizeof(psinfo) ) != sizeof(psinfo) )
    {
        close( fd );
        return (size_t)0L;      /* Can't read? */
    }
    close( fd );
    return (size_t)(psinfo.pr_rssize * 1024L);

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    /* BSD, Linux, and OSX -------------------------------------- */
    struct rusage rusage;
    getrusage( RUSAGE_SELF, &rusage );
#if defined(__APPLE__) && defined(__MACH__)
    return (size_t)rusage.ru_maxrss;
#else
    return (size_t)(rusage.ru_maxrss * 1024L);
#endif

#else
    /* Unknown OS ----------------------------------------------- */
    return (size_t)0L;          /* Unsupported. */
#endif
}





/**
 * Returns the current resident set size (physical memory use) measured
 * in bytes, or zero if the value cannot be determined on this OS.
 */
size_t get_mem_current( )
{
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS info;
    info.cb = sizeof(info);
    GetProcessMemoryInfo( GetCurrentProcess( ), &info, sizeof(info) );
    return (size_t)info.WorkingSetSize;

#elif defined(__APPLE__) && defined(__MACH__)
    /* OSX ------------------------------------------------------ */
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if ( task_info( mach_task_self( ), MACH_TASK_BASIC_INFO,
        (task_info_t)&info, &infoCount ) != KERN_SUCCESS )
        return (size_t)0L;      /* Can't access? */
    return (size_t)info.resident_size;

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
    /* Linux ---------------------------------------------------- */
    long rss = 0L;
    FILE* fp = NULL;
    if ( (fp = fopen( "/proc/self/statm", "r" )) == NULL )
        return (size_t)0L;      /* Can't open? */
    if ( fscanf( fp, "%*s%ld", &rss ) != 1 )
    {
        fclose( fp );
        return (size_t)0L;      /* Can't read? */
    }
    fclose( fp );
    return (size_t)rss * (size_t)sysconf( _SC_PAGESIZE);

#else
    /* AIX, BSD, Solaris, and Unknown OS ------------------------ */
    return (size_t)0L;          /* Unsupported. */
#endif
}






/**
 * Returns the size of physical memory (RAM) in bytes.
 */
size_t get_mem_size( )
{
#if defined(_WIN32) && (defined(__CYGWIN__) || defined(__CYGWIN32__))
    /* Cygwin under Windows. ------------------------------------ */
    /* New 64-bit MEMORYSTATUSEX isn't available.  Use old 32.bit */
    MEMORYSTATUS status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatus( &status );
    return (size_t)status.dwTotalPhys;

#elif defined(_WIN32)
    /* Windows. ------------------------------------------------- */
    /* Use new 64-bit MEMORYSTATUSEX, not old 32-bit MEMORYSTATUS */
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx( &status );
    return (size_t)status.ullTotalPhys;

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    /* UNIX variants. ------------------------------------------- */
    /* Prefer sysctl() over sysconf() except sysctl() HW_REALMEM and HW_PHYSMEM */

#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))
    int mib[2];
    mib[0] = CTL_HW;
#if defined(HW_MEMSIZE)
    mib[1] = HW_MEMSIZE;            /* OSX. --------------------- */
#elif defined(HW_PHYSMEM64)
    mib[1] = HW_PHYSMEM64;          /* NetBSD, OpenBSD. --------- */
#endif
    int64_t size = 0;               /* 64-bit */
    size_t len = sizeof( size );
    if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
        return (size_t)size;
    return 0L;          /* Failed? */

#elif defined(_SC_AIX_REALMEM)
    /* AIX. ----------------------------------------------------- */
    return (size_t)sysconf( _SC_AIX_REALMEM ) * (size_t)1024L;

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    /* FreeBSD, Linux, OpenBSD, and Solaris. -------------------- */
    return (size_t)sysconf( _SC_PHYS_PAGES ) *
        (size_t)sysconf( _SC_PAGESIZE );

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
    /* Legacy. -------------------------------------------------- */
    return (size_t)sysconf( _SC_PHYS_PAGES ) *
        (size_t)sysconf( _SC_PAGE_SIZE );

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
    /* DragonFly BSD, FreeBSD, NetBSD, OpenBSD, and OSX. -------- */
    int mib[2];
    mib[0] = CTL_HW;
#if defined(HW_REALMEM)
    mib[1] = HW_REALMEM;        /* FreeBSD. ----------------- */
#elif defined(HW_PYSMEM)
    mib[1] = HW_PHYSMEM;        /* Others. ------------------ */
#endif
    unsigned int size = 0;      /* 32-bit */
    size_t len = sizeof( size );
    if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
        return (size_t)size;
    return 0L;          /* Failed? */
#endif /* sysctl and sysconf variants */

#else
    return 0L;          /* Unknown OS. */
#endif
}








/**
 * Returns the amount of CPU time used by the current process,
 * in seconds, or -1.0 if an error occurred.
 */
double get_time_thread( )
{
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    FILETIME createTime;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;
    if ( GetProcessTimes( GetCurrentProcess( ),
        &createTime, &exitTime, &kernelTime, &userTime ) != -1 )
    {
        SYSTEMTIME userSystemTime;
        if ( FileTimeToSystemTime( &userTime, &userSystemTime ) != -1 )
            return (double)userSystemTime.wHour * 3600.0 +
                (double)userSystemTime.wMinute * 60.0 +
                (double)userSystemTime.wSecond +
                (double)userSystemTime.wMilliseconds / 1000.0;
    }

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    /* AIX, BSD, Cygwin, HP-UX, Linux, OSX, and Solaris --------- */

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
    /* Prefer high-res POSIX timers, when available. */
    {
        clockid_t id;
        struct timespec ts;
#if _POSIX_CPUTIME > 0
        /* Clock ids vary by OS.  Query the id, if possible. */
        if ( clock_getcpuclockid( 0, &id ) == -1 )
#endif
#if defined(CLOCK_PROCESS_CPUTIME_ID)
            /* Use known clock id for AIX, Linux, or Solaris. */
            id = CLOCK_PROCESS_CPUTIME_ID;
#elif defined(CLOCK_VIRTUAL)
            /* Use known clock id for BSD or HP-UX. */
            id = CLOCK_VIRTUAL;
#else
            id = (clockid_t)-1;
#endif
        if ( id != (clockid_t)-1 && clock_gettime( id, &ts ) != -1 )
            return (double)ts.tv_sec +
                (double)ts.tv_nsec / 1000000000.0;
    }
#endif

#if defined(RUSAGE_SELF)
    {
        struct rusage rusage;
        if ( getrusage( RUSAGE_SELF, &rusage ) != -1 )
            return (double)rusage.ru_utime.tv_sec +
                (double)rusage.ru_utime.tv_usec / 1000000.0;
    }
#endif

#if defined(_SC_CLK_TCK)
    {
        const double ticks = (double)sysconf( _SC_CLK_TCK );
        struct tms tms;
        if ( times( &tms ) != (clock_t)-1 )
            return (double)tms.tms_utime / ticks;
    }
#endif

#if defined(CLOCKS_PER_SEC)
    {
        clock_t cl = clock( );
        if ( cl != (clock_t)-1 )
            return (double)cl / (double)CLOCKS_PER_SEC;
    }
#endif

#endif

    return -1;      /* Failed. */
}





/**
 * Returns the real time, in seconds, or -1.0 if an error occurred.
 *
 * Time is measured since an arbitrary and OS-dependent start time.
 * The returned real time is only useful for computing an elapsed time
 * between two calls to this function.
 */
double get_time_os()
{
#if defined(_WIN32)
    FILETIME tm;
    ULONGLONG t;
#if 0 //defined(NTDDI_WIN8) && NTDDI_VERSION >= NTDDI_WIN8
    /* Windows 8, Windows Server 2012 and later. ---------------- */
//  GetSystemTimePreciseAsFileTime( &tm );
#else
    /* Windows 2000 and later. ---------------------------------- */
    GetSystemTimeAsFileTime( &tm );
#endif
    t = ((ULONGLONG)tm.dwHighDateTime << 32) | (ULONGLONG)tm.dwLowDateTime;
    return (double)t / 10000000.0;

#elif (defined(__hpux) || defined(hpux)) || ((defined(__sun__) || defined(__sun) || defined(sun)) && (defined(__SVR4) || defined(__svr4__)))
    /* HP-UX, Solaris. ------------------------------------------ */
    return (double)gethrtime( ) / 1000000000.0;

#elif defined(__MACH__) && defined(__APPLE__)
    /* OSX. ----------------------------------------------------- */
    static double timeConvert = 0.0;
    if ( timeConvert == 0.0 )
    {
        mach_timebase_info_data_t timeBase;
        (void)mach_timebase_info( &timeBase );
        timeConvert = (double)timeBase.numer /
            (double)timeBase.denom /
            1000000000.0;
    }
    return (double)mach_absolute_time( ) * timeConvert;

#elif defined(_POSIX_VERSION)
    /* POSIX. --------------------------------------------------- */
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
    {
        struct timespec ts;
#if defined(CLOCK_MONOTONIC_PRECISE)
        /* BSD. --------------------------------------------- */
        const clockid_t id = CLOCK_MONOTONIC_PRECISE;
#elif defined(CLOCK_MONOTONIC_RAW)
        /* Linux. ------------------------------------------- */
        const clockid_t id = CLOCK_MONOTONIC_RAW;
#elif defined(CLOCK_HIGHRES)
        /* Solaris. ----------------------------------------- */
        const clockid_t id = CLOCK_HIGHRES;
#elif defined(CLOCK_MONOTONIC)
        /* AIX, BSD, Linux, POSIX, Solaris. ----------------- */
        const clockid_t id = CLOCK_MONOTONIC;
#elif defined(CLOCK_REALTIME)
        /* AIX, BSD, HP-UX, Linux, POSIX. ------------------- */
        const clockid_t id = CLOCK_REALTIME;
#else
        const clockid_t id = (clockid_t)-1; /* Unknown. */
#endif /* CLOCK_* */
        if ( id != (clockid_t)-1 && clock_gettime( id, &ts ) != -1 )
            return (double)ts.tv_sec +
                (double)ts.tv_nsec / 1000000000.0;
        /* Fall thru. */
    }
#endif /* _POSIX_TIMERS */

    /* AIX, BSD, Cygwin, HP-UX, Linux, OSX, POSIX, Solaris. ----- */
    struct timeval tm;
    gettimeofday( &tm, NULL );
    return (double)tm.tv_sec + (double)tm.tv_usec / 1000000.0;
#else
    return -1.0;        /* Failed. */
#endif
}

std::string as_human_size( size_t bytes ) {
    /**/ if( bytes >= 1024 * 1024 * 1024 ) return to_string( bytes / (1024 * 1024 * 1024)) + " GB";
    else if( bytes >=   16 * 1024 * 1024 ) return to_string( bytes / (       1024 * 1024)) + " MB";
    else if( bytes >=          16 * 1024 ) return to_string( bytes / (              1024)) + " KB";
    else                                   return to_string( bytes ) + " bytes";
}
std::string as_human_time( double time ) {
    /**/ if( time >   48 * 3600 ) return to_string( time / (24*3600), 0 ) + " days";
    else if( time >    120 * 60 ) return to_string( time / (60*60), 0 ) + " hours";
    else if( time >=        120 ) return to_string( time / 60, 0 ) + " mins";
    else if( time >= 1 || time <= 0 ) return to_string( time, 2 ) + " s";
    else if( time <= 1 / 1000.f )
        return to_string( time * 1000000, 0 ) + " ns";
    else
        return to_string( time * 1000, 0 ) + " ms";
}
std::string as_human_chrono( double time ) {
    uint64_t time64 = uint64_t( time );
    uint64_t secs64 = ( time64 % 60 );
    uint64_t mins64 = ( time64 / 60 ) % 60;
    uint64_t hour64 = ( time64 / 3600 ) % 24;
    uint64_t days64 = ( time64 / 86400 );
    std::string csecs = std::to_string( int( time * 100 ) % 100 );
    std::string sign = std::string() + (time < 0 ? '-' : '+');
    return sign +
        std::to_string(days64) + (hour64 < 10 ? "d:0" : "d:") +
        std::to_string(hour64) + (mins64 < 10 ? "h:0" : "h:") +
        std::to_string(mins64) + (secs64 < 10 ? "m:0" : "m:") +
        std::to_string(secs64) + "." + csecs + "s";
}

namespace {
    const double app_epoch = get_time_os();
}
double get_time_app() {
    return get_time_os() - app_epoch;
}

std::ostream &benchmark::print( std::ostream &os ) const {
    os << name() << " = " << as_human_size(mem) << ", " << as_human_time(time) << std::endl;
    return os;
}

void sleep( double seconds ) {
    $windows( Sleep( seconds * 1000 ) );
    $welse( usleep( seconds * 1000000 ) );
}

void tick() {
    $windows( Sleep(1) );
    $welse( usleep(1) );
}

std::string top100() {
    std::stringstream out;
    std::set< benchmark > set;
    for( auto &it : benchmark::all() ) {
        set.insert( it.second );
    }
    auto it = set.rbegin();
    for( unsigned counter = ( set.size() < 100 ? set.size() : 100 ); counter--; ) {
        it->print( out );
        ++it;
    }
    return out.str();
}

#undef $check
