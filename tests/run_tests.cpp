/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * All Rights Reserved.
 *
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
 *
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <libgen.h>
#include <argp.h>
#include <fnmatch.h>
#include <ftw.h>
#include <termios.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <string>
#include <algorithm>
#include <vector>

#include "../external/json-parser/json.c"

#define F_VERBOSE   0x00000001
#define F_LISTTESTS 0x00000002
#define F_DRYRUN    0x00000004

//----------------------------------------------------------------------------------------------------------------------
// Command line arguments.
//----------------------------------------------------------------------------------------------------------------------
struct arguments_t
{
    arguments_t() : flags(0), jobs(0) {}

    unsigned int flags;  // F_VERBOSE, F_DRYRUN, ...
    unsigned int jobs;   // Number of jobs to execute simultaneously.
    std::string logfile; // Name of our logfile.
    std::string vogl_trace_dir;
    std::string vogl_proj_dir;
    std::vector<std::string> filenames;
    std::vector<std::string> test_patterns;
};

//----------------------------------------------------------------------------------------------------------------------
// Individual test information.
//----------------------------------------------------------------------------------------------------------------------
struct test_info_t
{
    test_info_t() : testid(-1), file(NULL), fileid(0), icommand(0) {}

    int testid;
    std::string name;      // Something like "g-truc3:gl-320-buffer-uniform32.trace".

    // popen info
    FILE *file; // Pipe to our launched command.
    int fileid; // Pipe file id.

    struct command_info_t
    {
        command_info_t() : ret(0), launched(0), time0(0) {}

        int ret;             // Command return code.
        int launched;        // Command launched (0:no, 1:yes, -1:error)
        std::string command; // Cmdline to launch.
        std::string output;  // Output from command.
        uint64_t time0;
    };
    size_t icommand;                           // Current command
    std::vector<command_info_t> command_infos; // Array of commands to execute.
};

//----------------------------------------------------------------------------------------------------------------------
// Tracefile information read from tests.json.
//----------------------------------------------------------------------------------------------------------------------
struct retrace_info_t
{
    retrace_info_t()
    {
        window_width = 0;
        window_height = 0;
        comparison_sum_threshold = 0;
        comparison_frames_to_skip = 0;
        trim_frame_start = 0;
        trim_frame_count = 0;
    }

    int window_width;
    int window_height;
    int comparison_sum_threshold;
    int comparison_frames_to_skip;
    int trim_frame_start;
    int trim_frame_count;

    // Full tracefile name.
    std::string tracefile; 
};

//----------------------------------------------------------------------------------------------------------------------
// Main test class.
//----------------------------------------------------------------------------------------------------------------------
class CTests
{
public:
    CTests() : m_verbose(false),
               m_dryrun(false),
               m_command_errors(0),
               m_commands_launched(0),
               m_testid(0) {}
    ~CTests() {}

    void init(const arguments_t &args);

public:
    // Add a json file of tests.
    void add_test_file(std::string filename);

    // Add a bunch of voglcore tests.
    void add_voglcore_tests();
    
    // Execute the tests.
    void exec_tests(unsigned int jobs);

    // Print out the test results.
    void spew_results(FILE *f, char *argv[]);

private:
    void add_test(const char *name, json_value *obj);
    void setup_test_commands(const char *name, test_info_t &testinfo, const retrace_info_t &retraceinfo);
    bool check_command(test_info_t *testinfo);

    bool m_verbose;
    bool m_dryrun;
    bool m_listtests;

    int m_command_errors;
    int m_commands_launched;

    // Current testid number.
    int m_testid;
    // Test fnmatch patterns.
    std::vector<std::string> m_test_patterns;

    // Paths to libvogl binaries.
    std::string m_libvogltrace32;
    std::string m_libvogltrace64;
    std::string m_voglreplay32;
    std::string m_voglreplay64;
    std::string m_glretrace32;         // ./i386/glretrace
    std::string m_voglreplay32_stable; // ./i386/voglreplay32_stable

    std::string m_voglcoretest32;
    std::string m_voglcoretest64;

    // Array of tests.
    std::vector<test_info_t> m_testinfos;
};

//----------------------------------------------------------------------------------------------------------------------
// get_time function.
//----------------------------------------------------------------------------------------------------------------------
inline uint64_t get_time()
{
    static const uint64_t g_BILLION = 1000000000;

    struct timespec timespec;
    clock_gettime(CLOCK_MONOTONIC, &timespec);
    return (timespec.tv_sec * g_BILLION) + timespec.tv_nsec;
}

inline float time_to_sec(uint64_t time)
{
    static const double g_rcpBILLION = (1.0 / 1000000000);
    return (float)(time * g_rcpBILLION);
}

//----------------------------------------------------------------------------------------------------------------------
// Argp parse function.
//----------------------------------------------------------------------------------------------------------------------
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    arguments_t *arguments = (arguments_t *)state->input;

    switch (key)
    {
        case 'f':
        case 0:
            if (arg && arg[0])
                arguments->filenames.push_back(arg);
            break;

        case 'p':
            if (arg && arg[0])
                arguments->test_patterns.push_back(arg);
            break;

        case 'j':
            if (arg && arg[0])
                arguments->jobs = (unsigned int)atoi(arg);
            break;

        case 'l':
            if (arg && arg[0])
                arguments->logfile = arg;
            break;

        case '?':
            argp_state_help(state, stderr, ARGP_HELP_LONG | ARGP_HELP_EXIT_OK);
            break;

        case 'v':
            arguments->flags |= F_VERBOSE;
            break;
        case 't':
            arguments->flags |= (F_LISTTESTS | F_DRYRUN);
            break;
        case 'y':
            arguments->flags |= F_DRYRUN;
            break;
        case 'd':
            arguments->vogl_trace_dir = arg;
            break;
    }
    return 0;
}

//----------------------------------------------------------------------------------------------------------------------
// Print string to log and stdout.
//----------------------------------------------------------------------------------------------------------------------
void logprintf(FILE *f, const char *format, ...)
{
    va_list args;

    // Print to log file
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);

    // Print to stdout
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

//----------------------------------------------------------------------------------------------------------------------
// Spew error string and die.
//----------------------------------------------------------------------------------------------------------------------
void errorf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    exit(-1);
}

//----------------------------------------------------------------------------------------------------------------------
// Try to get full path given a relative filename.
//----------------------------------------------------------------------------------------------------------------------
std::string getfullpath(std::string filename)
{
    char *path = realpath(filename.c_str(), NULL);
    if (!path || access(filename.c_str(), F_OK))
    {
        const char *vogl_proj_dir = getenv("VOGL_PROJ_DIR");
        if (vogl_proj_dir)
        {
            std::string fname = vogl_proj_dir;
            fname += "/tests/" + filename;
            path = realpath(fname.c_str(), NULL);
        }
    }

    if (!path)
        return filename;

    std::string pathret = path;
    free(path);
    return pathret;
}

//----------------------------------------------------------------------------------------------------------------------
// Read in file.
//----------------------------------------------------------------------------------------------------------------------
std::string get_file_contents(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (fp)
    {
        std::string str;

        fseek(fp, 0, SEEK_END);
        str.resize(ftell(fp));
        rewind(fp);

        fread(&str[0], 1, str.size(), fp);
        fclose(fp);
        return str;
    }

    return "";
}

//----------------------------------------------------------------------------------------------------------------------
// Printf style formatting for std::string.
//----------------------------------------------------------------------------------------------------------------------
std::string string_format(const char *fmt, ...)
{
    std::string str;
    int size = 256;

    for (;;)
    {
        va_list ap;

        va_start(ap, fmt);
        str.resize(size);
        int n = vsnprintf((char *)str.c_str(), size, fmt, ap);
        va_end(ap);

        if ((n > -1) && (n < size))
        {
            str.resize(n);
            return str;
        }

        size = (n > -1) ? (n + 1) : (size * 2);
    }

    return str;
}

//----------------------------------------------------------------------------------------------------------------------
// Trim directory and extension from filename.
//----------------------------------------------------------------------------------------------------------------------
std::string getbasename(const std::string filename)
{
    char *tracefile = strdup(filename.c_str());

    // Trim the directory.
    std::string base = basename(tracefile);
    // Trim the extension.
    int lastindex = base.find_last_of(".");
    base = base.substr(0, lastindex);

    free(tracefile);
    return base;
}

//----------------------------------------------------------------------------------------------------------------------
// Return a formatted time string.
//----------------------------------------------------------------------------------------------------------------------
std::string gettimestr(const char *fmt)
{
    char timestr[128];
    timestr[0] = 0;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm)
    {
        strftime(timestr, sizeof(timestr), fmt, tm);
    }

    return timestr;
}

//----------------------------------------------------------------------------------------------------------------------
// Return temp directory.
//----------------------------------------------------------------------------------------------------------------------
std::string gettempdir()
{
    return string_format("%s/%s", P_tmpdir, "_vogltests_tmp");
}

static int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rv = remove(fpath);
    if (rv)
        perror(fpath);

    return rv;
}

//----------------------------------------------------------------------------------------------------------------------
// CTests init function.
//----------------------------------------------------------------------------------------------------------------------
void CTests::init(const arguments_t &args)
{
    m_verbose = !!(args.flags & F_VERBOSE);
    m_dryrun = !!(args.flags & F_DRYRUN);
    m_listtests = !!(args.flags & F_LISTTESTS);

    m_test_patterns = args.test_patterns;

    // Get full paths to our vogltrace libraries.
    m_libvogltrace32 = args.vogl_trace_dir + "/libvogltrace32.so";
    m_libvogltrace64 = args.vogl_trace_dir + "/libvogltrace64.so";
    if (access(m_libvogltrace32.c_str(), F_OK))
        errorf("ERROR: Could not find %s\n", m_libvogltrace32.c_str());

    // Get full paths to our replay binaries.
    m_voglreplay32 = args.vogl_trace_dir + "/voglreplay32";
    m_voglreplay64 = args.vogl_trace_dir + "/voglreplay64";
    if (access(m_voglreplay32.c_str(), F_OK))
        errorf("ERROR: Could not find %s\n", m_voglreplay32.c_str());

    m_glretrace32 = getfullpath("./i386/glretrace");
    if (access(m_glretrace32.c_str(), F_OK))
        errorf("ERROR: Could not find %s\n", m_glretrace32.c_str());

    m_voglreplay32_stable = getfullpath("./i386/voglreplay32_stable");
    if (access(m_voglreplay32_stable.c_str(), F_OK))
        errorf("ERROR: Could not find %s\n", m_voglreplay32_stable.c_str());

    m_voglcoretest32 = args.vogl_trace_dir + "/vogltest32";
    m_voglcoretest64 = args.vogl_trace_dir + "/vogltest64";

    printf("\nUsing:\n");
    printf("  %s\n", m_libvogltrace32.c_str());
    printf("  %s\n", m_voglreplay32.c_str());
    printf("  %s\n", m_glretrace32.c_str());
    printf("  %s\n", m_voglreplay32_stable.c_str());
    printf("  %s\n", m_voglcoretest32.c_str());
    printf("  %s\n", m_voglcoretest64.c_str());
    printf("\n");
}

//----------------------------------------------------------------------------------------------------------------------
// Set up commands needed for individual test.
//----------------------------------------------------------------------------------------------------------------------
void CTests::setup_test_commands(const char *name, test_info_t &testinfo, const retrace_info_t &retraceinfo)
{
    testinfo.command_infos.clear();

    // Construct something like "g-truc3 : gl-320-buffer-uniform32.trace"
    const char *bname = strrchr(retraceinfo.tracefile.c_str(), '/');
    bname = bname ? (bname + 1) : retraceinfo.tracefile.c_str();
    testinfo.name = string_format("%s : %s", name, bname);

    std::string tempdir = gettempdir();
    std::string base = getbasename(retraceinfo.tracefile);
    std::string trace_sum_arg = retraceinfo.comparison_sum_threshold ? " --vogl_sum_hashing" : "";
    std::string sum_arg = retraceinfo.comparison_sum_threshold ? " -sum_hashing" : "";
    std::string vogl_trace_file = string_format(" %s/%s.trace.bin", tempdir.c_str(), base.c_str());
    std::string trace_hash_file = string_format(" %s/%s_trace_hashes.txt", tempdir.c_str(), base.c_str());
    std::string replay_hash_file = string_format(" %s/%s_replay_hashes.txt", tempdir.c_str(), base.c_str());
    std::string window_size = string_format(" -width %u -height %u ", retraceinfo.window_width, retraceinfo.window_height);
    std::string sum_compare_threshold = string_format(" -sum_compare_threshold %u", retraceinfo.comparison_sum_threshold);
    std::string compare_ignore_frames = string_format(" -compare_ignore_frames %u", retraceinfo.comparison_frames_to_skip);
    std::string replayapp = strstr(retraceinfo.tracefile.c_str(), ".trace") ? m_glretrace32 : m_voglreplay32_stable;

    //$ TODO mikesart: launch this stuff with valgrind?

    std::string vogl_cmd_line = "VOGL_CMD_LINE=\"";
    vogl_cmd_line += "--vogl_tracefile" + vogl_trace_file + " --vogl_dump_backbuffer_hashes" + trace_hash_file + trace_sum_arg;
    vogl_cmd_line += "\" ";

    std::string ld_preload = "LD_PRELOAD=" + m_libvogltrace32;
    const char *ld_preload_env = getenv("LD_PRELOAD");
    if (ld_preload_env && ld_preload_env[0])
    {
        ld_preload += ":";
        ld_preload += ld_preload_env;
    }

    test_info_t::command_info_t cmdinfo;

    cmdinfo.command = vogl_cmd_line + ld_preload + " " + replayapp + " --benchmark " + retraceinfo.tracefile;
    testinfo.command_infos.push_back(cmdinfo);

    cmdinfo.command = m_voglreplay32 + vogl_trace_file + sum_arg + " -dump_backbuffer_hashes" + replay_hash_file +
            " -verbose -lock_window_dimensions" + window_size;
    testinfo.command_infos.push_back(cmdinfo);

    cmdinfo.command = m_voglreplay32 + sum_arg + " --compare_hash_files" + replay_hash_file + trace_hash_file + compare_ignore_frames + sum_compare_threshold;
    testinfo.command_infos.push_back(cmdinfo);

    if (retraceinfo.trim_frame_count)
    {
        // Trim test.
        std::string vogl_trace_file_trimmed = string_format("%s/%s_trimmed.trace.bin", tempdir.c_str(), base.c_str());
        std::string vogl_trace_file_trimmed2 = string_format("%s/%s_trimmed2.trace.bin", tempdir.c_str(), base.c_str());
        std::string replay_hash_file_trimmed = string_format(" %s/%s_replay_hashes_trimmed.txt", tempdir.c_str(), base.c_str());
        std::string trim_frame_str = string_format(" -trim_frame %u -trim_len %u", retraceinfo.trim_frame_start, retraceinfo.trim_frame_count) + " -trim_file ";
        std::string jdump_dir = tempdir + "/jdump_" + base;

        if (!m_dryrun)
            mkdir(jdump_dir.c_str(), 0755);

        // Trim the trace file.
        cmdinfo.command = m_voglreplay32 + vogl_trace_file + trim_frame_str + vogl_trace_file_trimmed;
        testinfo.command_infos.push_back(cmdinfo);

        // Losslessly dump trace to JSON.
        cmdinfo.command = m_voglreplay32 + " " + vogl_trace_file_trimmed + " --dump " + jdump_dir + "/jdump";
        testinfo.command_infos.push_back(cmdinfo);

        // Read JSON trace back to binary trace file.
        cmdinfo.command = m_voglreplay32 + " --parse " + jdump_dir + "/jdump " + vogl_trace_file_trimmed2;
        testinfo.command_infos.push_back(cmdinfo);

        cmdinfo.command = m_voglreplay32 + sum_arg + " -lock_window_dimensions" + window_size +
                vogl_trace_file_trimmed2 + " -dump_backbuffer_hashes" + replay_hash_file_trimmed;
        testinfo.command_infos.push_back(cmdinfo);

        cmdinfo.command = m_voglreplay32 + sum_arg + " -compare_hash_files" + sum_compare_threshold +
                replay_hash_file_trimmed + trace_hash_file +
                string_format(" -compare_first_frame %u -ignore_line_count_differences", retraceinfo.trim_frame_start);
        testinfo.command_infos.push_back(cmdinfo);
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Parse json object and add test(s) contained therein.
//----------------------------------------------------------------------------------------------------------------------
void CTests::add_test(const char *name, json_value *obj)
{
    if (obj->type != json_object)
        errorf("ERROR: parse_object was passed a non json_object.\n");

    test_info_t testinfo;
    retrace_info_t retraceinfo;

    std::string driver_str = "";

    for (unsigned int i = 0; i < obj->u.object.length; i++)
    {
        json_value *val = obj->u.object.values[i].value;
        std::string objname = obj->u.object.values[i].name;

        if (val->type == json_integer)
        {
            if (objname == "window_width")
                retraceinfo.window_width = val->u.integer;
            else if (objname == "window_height")
                retraceinfo.window_height = val->u.integer;
            else if (objname == "comparison_sum_threshold")
                retraceinfo.comparison_sum_threshold = val->u.integer;
            else if (objname == "comparison_frames_to_skip")
                retraceinfo.comparison_frames_to_skip = val->u.integer;
            else if (objname == "trim_frame_start")
                retraceinfo.trim_frame_start = val->u.integer;
            else if (objname == "trim_frame_count")
                retraceinfo.trim_frame_count = val->u.integer;
            else
                errorf("ERROR: Unknown object '%s'\n", objname.c_str());
        }
        else if ((val->type == json_string) && objname == "driver")
        {
            // Check for nvidia, amd, or intel here.
            driver_str = "(" + std::string(val->u.string.ptr, val->u.string.length) + ")";
            std::transform(driver_str.begin(), driver_str.end(), driver_str.begin(), ::toupper);
        }
        else if ((val->type == json_array) && (objname == "trace_files"))
        {
            for (unsigned int j = 0; j < val->u.array.length; j++)
            {
                json_value *val2 = val->u.array.values[j];

                if ((val2->type == json_string) && val2->u.string.ptr)
                {
                    const char *filename = val2->u.string.ptr;

                    testinfo.testid = m_testid++;

                    bool add = true;
                    if (m_test_patterns.size())
                    {
                        add = false;

                        for (size_t p = 0; p < m_test_patterns.size(); p++)
                        {
                            int ret = fnmatch(m_test_patterns[p].c_str(), filename, FNM_NOESCAPE);
                            if (!ret)
                            {
                                add = true;
                                break;
                            }
                        }
                    }

                    if (add)
                    {
                        std::string tracefile = getfullpath(filename);

                        if (access(tracefile.c_str(), F_OK))
                        {
                            printf("WARNING: Trace file '%s' not found. Skipping.\n", filename);
                        }
                        else
                        {
                            // Set up the commands we need to run.
                            retraceinfo.tracefile = tracefile;
                            setup_test_commands(name, testinfo, retraceinfo);
                            // Add this test trace file.
                            m_testinfos.push_back(testinfo);

                            if (m_listtests)
                            {
                                printf("%d) %s w:%d h:%d trim_start:%d trim_count:%d threshold:%d skip:%d %s %s\n",
                                       m_testid, testinfo.name.c_str(),
                                       retraceinfo.window_width, retraceinfo.window_height,
                                       retraceinfo.trim_frame_start, retraceinfo.trim_frame_count,
                                       retraceinfo.comparison_sum_threshold, retraceinfo.comparison_frames_to_skip,
                                       retraceinfo.tracefile.c_str(), driver_str.c_str());

                                if (m_verbose)
                                {
                                    for (size_t j = 0; j < testinfo.command_infos.size(); j++)
                                        printf("  %s\n", testinfo.command_infos[j].command.c_str());
                                    printf("\n");
                                }
                            }
                        }
                    }
                }
                else
                {
                    printf("WARNING: Ignoring non string trace filename in %s\n", objname.c_str());
                }
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Add a json test file.
//----------------------------------------------------------------------------------------------------------------------
void CTests::add_test_file(std::string filename)
{
    std::string str = get_file_contents(filename.c_str());

    const char *js = str.c_str();
    if (!js || !js[0])
        errorf("Error: Could not read %s\n", filename.c_str());

    json_settings jsettings;
    memset(&jsettings, 0, sizeof(jsettings));
    jsettings.settings = json_enable_comments;

    char jerror[json_error_max + 1];
    jerror[0] = 0;

    json_value *val = json_parse_ex(&jsettings, js, strlen(js), jerror);
    if (!val || (val->type != json_object))
        errorf("ERROR: json_parse_ex failed (%s)\n", jerror);

    for (unsigned int i = 0; i < val->u.object.length; i++)
    {
        add_test(val->u.object.values[i].name, val->u.object.values[i].value);
    }

    json_value_free(val);
}

//----------------------------------------------------------------------------------------------------------------------
// Add bunch of voglcore tests. Ie:
//   vogltest32 --test md5
//----------------------------------------------------------------------------------------------------------------------
void CTests::add_voglcore_tests()
{
    static const char *s_tests[] =
    {
    #define DEFTEST(_x) #_x
        DEFTEST(rh_hash_map),
        DEFTEST(object_pool),
        DEFTEST(dynamic_string),
        DEFTEST(md5),
        DEFTEST(introsort),
        DEFTEST(rand),
        DEFTEST(regexp),
        DEFTEST(strutils),
        DEFTEST(map),
        DEFTEST(hash_map),
        DEFTEST(sort),
        DEFTEST(sparse_vector),
        DEFTEST(bigint128),
    #undef DEFTEST
    };

    test_info_t testinfo;

    for (size_t i = 0; i < sizeof(s_tests) / sizeof(s_tests[0]); i++)
    {
        test_info_t::command_info_t cmdinfo;

        testinfo.name = s_tests[i];
        testinfo.testid = m_testid++;

        cmdinfo.command = m_voglcoretest64 + " --test " + s_tests[i];

        bool add = true;
        if (m_test_patterns.size())
        {
            add = false;

            // Check if any part of the command line matches the pattern string.
            for (size_t p = 0; p < m_test_patterns.size(); p++)
            {
                int ret = fnmatch(m_test_patterns[p].c_str(), cmdinfo.command.c_str(), FNM_NOESCAPE);
                if (!ret)
                {
                    add = true;
                    break;
                }
            }
        }

        if (add)
        {
            testinfo.command_infos.clear();
            testinfo.command_infos.push_back(cmdinfo);

            // Add this test trace file.
            m_testinfos.push_back(testinfo);

            if (m_listtests)
            {
                printf("%d) %s\n", m_testid, cmdinfo.command.c_str());
            }
        }
    }
}
    
//----------------------------------------------------------------------------------------------------------------------
// Launch and check status of command for test.
//----------------------------------------------------------------------------------------------------------------------
bool CTests::check_command(test_info_t *testinfo)
{
    if (testinfo->icommand >= testinfo->command_infos.size())
    {
        // No more commands to run.
        return false;
    }

    // Get current command information.
    test_info_t::command_info_t &commandinfo = testinfo->command_infos[testinfo->icommand];

    // If it hasn't launched, launch it.
    if (!commandinfo.launched)
    {
        commandinfo.launched = 1;

        if (!m_listtests)
        {
            printf("Launching #%d (%lu/%lu): '%s'\n", testinfo->testid, testinfo->icommand,
                   testinfo->command_infos.size() - 1, testinfo->name.c_str());
            if (m_verbose)
            {
                printf("  %s\n", commandinfo.command.c_str());
            }
        }

        if (!m_dryrun)
        {
            m_commands_launched++;

            commandinfo.time0 = get_time();

            testinfo->file = popen((commandinfo.command + " 2>&1").c_str(), "r");
            if (!testinfo->file)
            {
                // Popen error.
                commandinfo.launched = -1;
                commandinfo.output = string_format("ERROR popen (errno:%d): %s", errno, strerror(errno));

                // Don't execute any more comands.
                testinfo->icommand = testinfo->command_infos.size();
                return false;
            }

            // Set FILE to non-blocking.
            testinfo->fileid = fileno(testinfo->file);
            fcntl(testinfo->fileid, F_SETFL, O_NONBLOCK);
        }

        return true;
    }

    if (m_dryrun)
    {
        // Command is done, move to next one.
        testinfo->icommand++;
        return check_command(testinfo);
    }

    // Try to read from command pipe.
    char buf[4096 + 1];
    ssize_t r = read(testinfo->fileid, buf, sizeof(buf) - 1);

    if ((r == -1) && (errno == EAGAIN))
    {
        // No data.
    }
    else if (r > 0)
    {
        // Got some data.
        buf[r] = 0;
        commandinfo.output += buf;
    }
    else
    {
        // Pipe is closed:
        //  Upon successful return, pclose() shall return the termination status
        // of the command language interpreter. Otherwise, pclose() shall return
        // -1 and set errno to indicate the error.
        errno = 0;
        commandinfo.ret = pclose(testinfo->file);
        testinfo->file = NULL;

        if (commandinfo.ret == -1)
            commandinfo.ret = errno ? errno : -1;

        float time = time_to_sec(get_time() - commandinfo.time0);

        printf("          #%d (%lu/%lu): '%s' %.2fs (Return: %d)\n", testinfo->testid, testinfo->icommand,
               testinfo->command_infos.size() - 1, testinfo->name.c_str(), time, commandinfo.ret);

        if (commandinfo.ret == 0)
        {
            // Move on to the next command.
            testinfo->icommand++;
            return check_command(testinfo);
        }
        else
        {
            m_command_errors++;
            // Error: bail on the rest of the commands.
            testinfo->icommand = testinfo->command_infos.size();
        }
    }

    return true;
}

//----------------------------------------------------------------------------------------------------------------------
// SIGINT Signal handler.
//----------------------------------------------------------------------------------------------------------------------
static volatile int g_ctrlc_hit = 0;
static void ctrlc_handler(int s)
{
    printf("\nStopping tests (caught signal %d).\n", s);
    g_ctrlc_hit = 1;
}

static int vogl_getch()
{
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

// See http://www.flipcode.com/archives/_kbhit_for_Linux.shtml
static int vogl_kbhit()
{
    static const int STDIN = 0;
    static bool initialized = false;

    if (!initialized)
    {
        // Use termios to turn off line buffering
        termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initialized = true;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}

//----------------------------------------------------------------------------------------------------------------------
// Execute all tests read from json files.
//----------------------------------------------------------------------------------------------------------------------
void CTests::exec_tests(unsigned int jobs)
{
    size_t nextjob = 0;
    std::vector<test_info_t *> joblist;

    setenv("VOGL_BREAK_ON_ASSERT", "1", 0);

    // Default to 4 jobs if nothing was specified.
    if (jobs < 1)
        jobs = 4;
    if (jobs > m_testinfos.size())
        jobs = m_testinfos.size();

    if (!m_listtests)
    {
        std::string banner1(78, '#');
        printf("\n%s\n", banner1.c_str());
        printf("Executing tests. Jobs:%u. S:Status, Q:Quit.\n", jobs);
        printf("%s\n", banner1.c_str());
    }

    // Add jobs to our joblist.
    for (size_t i = 0; i < jobs; i++)
    {
        joblist.push_back(&m_testinfos[nextjob++]);
    }

    // Set up Ctrl+C Signal handler.
    struct sigaction new_sa, old_sa;
    new_sa.sa_handler = ctrlc_handler;
    sigemptyset(&new_sa.sa_mask);
    new_sa.sa_flags = 0;
    sigaction(SIGINT, &new_sa, &old_sa);

    // Continue while the joblist still has items in it.
    while (!g_ctrlc_hit && (joblist.size() > 0))
    {
        size_t index = 0;

        // Go through all the joblist items.
        while (index < joblist.size())
        {
            test_info_t *testinfo = joblist[index];

            // Check the job command status.
            if (!check_command(testinfo))
            {
                if (nextjob < m_testinfos.size())
                {
                    // Job is done - replace this job with next job.
                    joblist[index++] = &m_testinfos[nextjob++];
                }
                else
                {
                    // No more jobs - just kill this one.
                    joblist.erase(joblist.begin() + index);
                }
            }
            else
            {
                // Bump to check next item in joblist.
                index++;
            }
        }

        if (vogl_kbhit())
        {
            int ch = vogl_getch();

            if (ch == 'q' || ch == 'Q')
            {
                g_ctrlc_hit = true;
            }
            else if (ch == 's' || ch == 'S')
            {
                printf("\nStatus:\n");
                for (size_t i = 0; i < joblist.size(); i++)
                {
                    test_info_t *testinfo = joblist[i];
                    test_info_t::command_info_t &commandinfo = testinfo->command_infos[testinfo->icommand];


                    float time = time_to_sec(get_time() - commandinfo.time0);
                    printf("  %s %.2fs\n", testinfo->name.c_str(), time);
                }
                printf("\n");
            }
        }

        usleep(1000);
    }

    // Restore old signal handler.
    sigaction(SIGINT, &old_sa, NULL);
}

//----------------------------------------------------------------------------------------------------------------------
// Write tests results to log file.
//----------------------------------------------------------------------------------------------------------------------
void CTests::spew_results(FILE *f, char *argv[])
{
    std::string banner1(78, '#');
    std::string banner2(78, '*');
    std::string timestr = gettimestr("%Y-%m-%d %H:%M:%S");

    fprintf(f, "%s\n", banner1.c_str());
    fprintf(f, "# %s\n", timestr.c_str());

    fprintf(f, "# ");
    for (int i = 0; argv[i]; i++)
    {
        const char *quote = "";
        if (strchr(argv[i], ' ') || strchr(argv[i], '*') || strchr(argv[i], '#'))
            quote = "\"";
        fprintf(f, "%s%s%s ", quote, argv[i], quote);
    }
    fprintf(f, "\n");

    fprintf(f, "%s\n\n", banner1.c_str());

    std::string errors;

    for (size_t i = 0; i < m_testinfos.size(); i++)
    {
        const test_info_t &testinfo = m_testinfos[i];

        for (size_t j = 0; j < testinfo.command_infos.size(); j++)
        {
            const test_info_t::command_info_t &commandinfo = testinfo.command_infos[j];

            if (!commandinfo.launched)
                break;

            fprintf(f, "\n");
            fprintf(f, "%s\n", banner2.c_str());
            fprintf(f, "* %s (#%d %lu/%lu)\n", testinfo.name.c_str(), testinfo.testid, j, testinfo.command_infos.size() - 1);
            fprintf(f, "* %s\n", commandinfo.command.c_str());
            fprintf(f, "* Return: %d %s\n", commandinfo.ret, commandinfo.ret ? "(ERROR)" : "");
            fprintf(f, "%s\n", banner2.c_str());

            fprintf(f, "%s\n", commandinfo.output.c_str());

            if (commandinfo.ret)
            {
                errors += string_format("%s (#%d %lu/%lu) Return: %d\n", testinfo.name.c_str(),
                                        testinfo.testid, j, testinfo.command_infos.size() - 1, commandinfo.ret);
            }
        }
    }

    if (errors.size())
    {
        logprintf(f, "\n%s\n", banner1.c_str());
        logprintf(f, "# ERRORS\n");
        logprintf(f, "%s\n", banner1.c_str());
        logprintf(f, "%s\n", errors.c_str());
    }

    logprintf(f, "%d commands launched.\n", m_commands_launched);
    logprintf(f, "%d errors.\n", m_command_errors);
}

//----------------------------------------------------------------------------------------------------------------------
// main.
//----------------------------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    static struct argp_option options[] =
    {
        { "filename",    'f', "FILE",    OPTION_ARG_OPTIONAL, "Test filename (defaults to tests.json).", 0 },
        { "vogltracedir",'d', "DIR",     0,                   "libvogltrace32.so directory (defaults to ../vogl_build/bin).", 0 },
        { "logfile",     'l', "LOGFILE", 0,                   "Logfile name.", 1 },
        { "list",        't', 0,         0,                   "List tests in file.", 1 },
        { "pattern",     'p', "PATTERN", 0,                   "Test name pattern.", 1 },
        { "jobs",        'j', "JOBS",    0,                   "Allow N test jobs to run at once.", 1 },
        { "dry-run",     'y', 0,         0,                   "Don't execute commands.", 2 },
        { "verbose",     'v', 0,         0,                   "Produce verbose output.", 2 },
        { "help",        '?', 0,         0,                   "Give this help message.", -1 },
        { 0 }
    };

    std::string tempdir = gettempdir();

    // Get current time.
    uint64_t time0 = get_time();

    arguments_t args;
    args.vogl_proj_dir = getenv("VOGL_PROJ_DIR");

    struct argp argp = { options, parse_opt, 0, "vogl project builder." };

    // Parse args.
    argp_parse(&argp, argc, argv, ARGP_NO_HELP, 0, &args);

    // Default to reading tests.json.
    if (!args.filenames.size())
        args.filenames.push_back(getfullpath("tests.json"));

    // Set up vogltrace directory paths.
    if (!args.vogl_trace_dir.size())
        args.vogl_trace_dir = getfullpath("../vogl_build/bin");

    {
        char *vogl_trace_dir = realpath(args.vogl_trace_dir.c_str(), NULL);
        if (!vogl_trace_dir || access(vogl_trace_dir, F_OK))
            errorf("ERROR: Invalid vogltracedir: %s\n", vogl_trace_dir);

        args.vogl_trace_dir = vogl_trace_dir;
        free(vogl_trace_dir);
    }

    if (!args.logfile.size())
    {
        std::string timestr = gettimestr("%Y_%m_%d-%H_%M_%S");
        args.logfile = string_format("%s/vogltests.%s.log", tempdir.c_str(), timestr.c_str());
    }

    if (access(tempdir.c_str(), F_OK) == 0)
    {
        printf("Removing %s directory.\n", tempdir.c_str());
        nftw(tempdir.c_str(), unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
    }
    mkdir(tempdir.c_str(), 0755);

    // Open our logfile.
    FILE *f = fopen(args.logfile.c_str(), "w");
    if (!f)
    {
        errorf("ERROR: Could not open logfile '%s'\n", args.logfile.c_str());
    }

    CTests tests;

    // Initialize tests with args.
    tests.init(args);

    // Add json test files.
    for (size_t i = 0; i < args.filenames.size(); i++)
    {
        tests.add_test_file(args.filenames[i]);
    }

    // Add the voglcore tests.
    tests.add_voglcore_tests();

    // Execute tests.
    tests.exec_tests(args.jobs);

    // Print results.
    tests.spew_results(f, argv);

    // Spew out time.
    time0 = get_time() - time0;
    logprintf(f, "\nTime: %.2fs\n", time_to_sec(time0));

    printf("Wrote logfile %s\n\n", args.logfile.c_str());

    fclose(f);
    return 0;
}
