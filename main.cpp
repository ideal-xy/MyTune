/*******************************************************
 * A high-performance, zero busy-waiting CLI music player
 * built on an event-driven model with system-level process control.
 *******************************************************/

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
#include <random>
#include <algorithm>
#include <iomanip>
#include <chrono>

/* syscall */
#include <unistd.h>
#include <sys/fcntl.h>
#include <dirent.h>
#include <csignal>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <sys/un.h>


#ifdef __linux__
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#define player "ffplay"
#elif  __APPLE__
#include <sys/event.h>
#define player "mpv"
#endif


const std::string RESET = "\033[0m";
const std::string BOLD = "\033[1m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string CYAN = "\033[36m";
const std::string RED = "\033[31m";
const std::string GRAY = "\033[90m";
const std::string MAGENTA = "\033[35m";
const std::string SOFT_BLUE = "\033[38;2;108;163;221m";
const std::string SOFT_PINK = "\033[38;2;224;172;181m";
const std::string SOFT_GREEN = "\033[38;2;136;204;181m";
const std::string SOFT_PURPLE = "\033[38;2;179;157;219m";
const std::string SOFT_YELLOW = "\033[38;2;246;213;127m";
const std::string CLEAR_SCREEN = "\033[2J\033[H";

struct RGB
{
    int r,g,b;
};

/* Greeting Logo  Color Scheme */
constexpr RGB LOGO_START = {251, 128, 114 };  // orange
constexpr RGB LOGO_END   = {80, 200, 179};  // blue

constexpr RGB start_color = {255, 192, 203}; // Pink
constexpr RGB end_color = {173, 216, 230};   // Light Blue

const std::vector<std::string> mytune_logo = {
    "  __  __       _______               ",
    " |  \\/  |     |__   __|              ",
    " | \\  / |_   _   | |_   _ _ __   ___ ",
    " | |\\/| | | | |  | | | | | '_ \\ / _ \\",
    " | |  | | |_| |  | | |_| | | | |  __/",
    " |_|  |_|\\__, |  |_|\\__,_|_| |_|\\___|",
    "          __/ |                      ",
    "         |___/                       ",
    "                                     ",
    "         Welcome to MyTune            ",
    " ----------------------------------- "
};


/* We use linear interpolation */
RGB gradient(const RGB start,const RGB end,const double ratio)
{
    return {
        static_cast<int>(start.r + (end.r - start.r) * ratio),
        static_cast<int>(start.g + (end.g - start.g) * ratio),
        static_cast<int>(start.b + (end.b - start.b) * ratio)
    };
}

/* RGB -> ANSI Escape Sequences */
std::string get_viewed(const RGB color)
{
    return "\033[38;2;" + std::to_string(color.r) + ";" + 
           std::to_string(color.g) + ";" + std::to_string(color.b) + "m";
}

void show_logo() 
{
    for (size_t i = 0; i < mytune_logo.size(); ++i) 
    {
        const double ratio = static_cast<double>(i) / static_cast<int>(mytune_logo.size() - 1);
        const RGB row_rgb = gradient(LOGO_START, LOGO_END, ratio);
        std::cout << get_viewed(row_rgb) << mytune_logo[i] << RESET << std::endl;
    }
    std::cout << std::endl;
}


/* seconds -> min:seconds */
std::string format_time(double seconds)
{
    int total = static_cast<int>(seconds);
    int min = total / 60;
    int second = total % 60;
    char buf[8];
    snprintf(buf,sizeof(buf),"%02d:%02d",min,second);
    return std::string{buf};

}

bool exists(const std::string& path)
{
    return (access(path.c_str(),F_OK) == 0);
}

bool is_music(const std::string& path)
{
    if (path.size() >= 4 && ((path.rfind(".m4a")) == (path.size() - 4) || (path.rfind(".mp3")) == (path.size() - 4)))
        return true;
    return false;
}

void draw(double cur, double total)
{
    constexpr int bar_width = 60;
    
    /* If mpv does not exit in time, cur may exceed total. So we have to truncate cur */
    double display_cur = (cur > total) ? total : cur;
    float progress = (total > 0) ? static_cast<float>(display_cur / total) : 0; // percentage
    int viewed_width = static_cast<int>(progress * bar_width); // part ought to be rendered

    std::cout << "\r  ";
    std::cout << get_viewed(LOGO_START) << "⏻ " << "";

    for (int i = 1; i <= bar_width; i++)
    {
        const auto ratio = static_cast<double>(i) / bar_width;
        const RGB cur_rgb = gradient(LOGO_START, LOGO_END, ratio);

        if (i <= viewed_width) std::cout << get_viewed(cur_rgb) << "█";
        else std::cout << "\033[90m" << "━";
    }

    if (progress >= 1.0f) std::cout << get_viewed(LOGO_END) << "";
    else std::cout << "\033[90m" << "";

    std::cout << "\033[0m " << std::fixed << std::setprecision(1) 
              << (progress * 100) << "% " 
              << "\033[90m(" << format_time(display_cur) << "/" << format_time(total) << ")\033[K" << std::flush;
}

int count(const std::string& path) 
{
    char count_buf[16] = {0};
    const std::string cmd = "find \"" + path + "\" -type f -name '*.m4a' -o -name '*.mp3'  | wc -l";

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("Popen() Fail: " + std::string(strerror(errno)));
    }

    if (std::fgets(count_buf, sizeof(count_buf), pipe.get()) == nullptr) {
        throw std::runtime_error("fgets() Fail");
    }

    try {
        return std::stoi(count_buf);
    } catch (const std::invalid_argument& e) 
    {
        std::cerr << e.what() << std::endl;
        throw std::runtime_error("Converting char* to int fail ");
    } catch (const std::out_of_range& e) 
    {
        std::cerr << e.what() << std::endl;
        throw std::runtime_error("Too many files to count: " + std::string(count_buf));
    }
}


int scan(const std::string& dir_path,std::vector<std::string>& list)
{
    list.clear();

    DIR* dir = opendir(dir_path.c_str());
    if (!dir)
    {
        throw std::runtime_error("Open directory" + dir_path + "failed" );
    }

    dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (strcmp(entry->d_name,".") == 0 || strcmp(entry->d_name,"..") == 0) continue;

        std::string full_path = dir_path + "/" + entry->d_name;

        struct stat st;
        if (stat(full_path.c_str(),&st) == -1 || !S_ISREG(st.st_mode)) continue;
       
        std::string name(entry->d_name);

        const auto pos = name.rfind('.');
        if (pos == std::string::npos) continue;
        std::string ext = name.substr(pos);
        std::transform(ext.begin(),ext.end(),ext.begin(),::tolower);
        if (ext == ".m4a" || ext == ".mp3") list.push_back(full_path);
    }

    return static_cast<int>(list.size());
}

std::string shell_quote(const std::string& path) 
{
    std::string escaped = "'";
    for (const auto c : path)
    {
        if (c == '\'') 
        {
            escaped += "'\\''";
        }else
        {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

double get_duration(const std::string& path)
{
    const std::string cmd = "afinfo \"" + path + "\" | awk '/estimated duration/ {print $3}'";
    const std::unique_ptr<FILE,decltype(&pclose)> pipe(popen(cmd.c_str(),"r"),pclose);
    if (!pipe)
    {
        throw std::runtime_error("Pipe error: " + std::string(strerror(errno)));
    }
    char buf[128] = {0};
    if (std::fgets(buf, sizeof(buf), pipe.get()) == nullptr) 
    {
        throw std::runtime_error("fgets() Fail");
    }
    
    try {
        return std::stod(buf);
    } catch (const std::invalid_argument& e) 
    {
        throw std::runtime_error("Time conversion fail: " + std::string(buf) + " (" + e.what() + ")");
    } catch (const std::out_of_range& e) 
    {
        throw std::runtime_error("Time out of range: " + std::string(buf) + " (" + e.what() + ")");
    }
}

#ifdef __linux__
void play(const std::string& path)
{
    double duration = get_duration(path);
    auto start_time = std::chrono::steady_clock::now();
    double paused_duration = 0;
    auto last_paused_time = start_time;
    bool paused = false;

    /* ---------- fork player ---------- */

    pid_t pid = fork();
    if (pid == 0) {
        execlp("ffplay", "ffplay", path.c_str(), nullptr);
        _exit(127);
    }

    /* ---------- time fd for progress ---------- */

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);

    itimerspec ts{};
    ts.it_interval.tv_nsec = 100 * 1000 * 1000; // 100ms
    ts.it_value = ts.it_interval;
    timerfd_settime(tfd, 0, &ts, nullptr);

    /* ---------- signal fd for child exit ---------- */

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    int sfd = signalfd(-1, &mask, 0);

    /* ---------- poll set ---------- */

    pollfd fds[3];
    fds[0] = { STDIN_FILENO, POLLIN, 0 };
    fds[1] = { tfd, POLLIN, 0 };
    fds[2] = { sfd, POLLIN, 0 };

    /* ---------- event loop ---------- */

    while (true)
    {
        poll(fds, 3, -1);

        /* ---- keyboard ---- */
        if (fds[0].revents & POLLIN) {
            char c;
            read(STDIN_FILENO, &c, 1);

            switch (c) {
                case 'n':
                    kill(pid, SIGTERM);
                    return;

                case 'q':
                    kill(pid, SIGTERM);
                    exit(0);

                case 's':
                    kill(pid, SIGSTOP);
                    last_paused_time = std::chrono::steady_clock::now();
                    paused = true;
                    break;

                case 'c':
                    if (paused) {
                        kill(pid, SIGCONT);
                        paused = false;
                        auto now = std::chrono::steady_clock::now();
                        paused_duration += std::chrono::duration<double>(now - last_paused_time).count();
                    }
                    break;
            }
        }

        /* ---- timer tick ---- */
        if (fds[1].revents & POLLIN) {
            uint64_t exp;
            read(tfd, &exp, sizeof(exp));

            if (!paused) {
                auto now = std::chrono::steady_clock::now();
                double elapsed =
                    std::chrono::duration<double>(now - start_time).count()
                    - paused_duration;

                draw(elapsed, duration);
            }
        }

        /* ---- child exit ---- */
        if (fds[2].revents & POLLIN) {
            signalfd_siginfo info;
            read(sfd, &info, sizeof(info));
            waitpid(pid, nullptr, 0);
            return;
        }
    }
}


#elif __APPLE__
int connect_mpv()
{
    const char* sock_path = "/tmp/mpv.sock";
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    constexpr size_t path_len = sizeof(addr.sun_path) - 1;
    strncpy(addr.sun_path, sock_path, path_len);

    const int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) return -1;

    for (int i = 0; i < 40; i++)
    {
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0)
            return sock;

        if (errno == ENOENT || errno == ECONNREFUSED)
        {
            usleep(i < 10 ? 50000: 15000);
            continue;
        }

        break;
    }

    close(sock);
    return -1;
}



void mpv_cmd(int sock, const char* json)
{
    write(sock, json, strlen(json));
}

double mpv_get_elapsed(int mpv_sock)
{
    const char *cmd = R"({"command": ["get_property", "playback-time"]})"
                      "\n";

    if (write(mpv_sock, cmd, strlen(cmd)) < 0)
    {
        return -1.0;
    }

    char buf[1024];
    const ssize_t n = read(mpv_sock, buf, sizeof(buf) - 1);
    if (n <= 0) return -1.0;

    buf[n] = '\0';

    const char *p = strstr(buf, "\"data\":");
    if (!p) return -1.0;

    p += 7;

    return strtod(p, nullptr);
}


void play(const std::string& path)
{
    const double duration = get_duration(path);
    bool paused = false;

    unlink("/tmp/mpv.sock");

    const pid_t pid = fork();
    if (pid < 0)
        throw std::runtime_error("fork failed");

    if (pid == 0)
    {
        const int devnull = open("/dev/null", O_RDWR);
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);

        execlp("mpv",
              "mpv",
                   "--no-video",
                   "--really-quiet",
                   "--terminal=no",
                   "--input-ipc-server=/tmp/mpv.sock",
                   path.c_str(),
                   nullptr);

        _exit(127);
    }

    /*---------- Connect to MPV server ---------*/

    const int mpv_sock = connect_mpv();
    if (mpv_sock == -1)
    {
        kill(pid, SIGKILL);
        throw std::runtime_error("Could not connect to MPV server after retries");
    }

    /* -------------- kqueue setup -------------- */
    const int kq = kqueue();
    if (kq == -1)
        throw std::runtime_error("kqueue failed");

    struct kevent evset[3];

    /* ------------------ keyboard input ---------------------------- */
    EV_SET(&evset[0], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);

    /* ----------- timer: refresh progress bar every 100ms -----------*/
    EV_SET(&evset[1], 1, EVFILT_TIMER, EV_ADD, 0, 100, nullptr);

    /* -----------------child process exit ---------------------------*/
    EV_SET(&evset[2], pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, nullptr);

    if (kevent(kq, evset, 3, nullptr, 0, nullptr) == -1)
        throw std::runtime_error("kevent register failed");

    while (true)
    {
        struct kevent events[3];
        const int n = kevent(kq, nullptr, 0, events, 3, nullptr);
        /* father process will be suspended,if there's no event. n means numbers of event. n <= 3  */

        for (int i = 0; i < n; i++)
        {
            const auto& e = events[i];

            /* ----child finished------ */
            if (e.filter == EVFILT_PROC)
            {
                waitpid(pid, nullptr, 0);
                close(kq);
                close(mpv_sock);
                draw(duration,duration);
                return;
            }

            /* ----keyboard input------ */
            if (e.filter == EVFILT_READ)
            {
                char c;
                read(STDIN_FILENO, &c, 1);

                switch (c)
                {
                    case 'n':
                        kill(pid, SIGTERM);
                        waitpid(pid, nullptr, 0);
                        close(mpv_sock);
                        close(kq);
                        std::cout << "\033[A\r\033[K" << GRAY << "  [ Next -> ]" << RESET << std::flush;
                        return;

                    case 'q':
                        kill(pid, SIGTERM);
                        waitpid(pid, nullptr, 0);
                        close(mpv_sock);
                        close(kq);

                        exit(0);
                    case 's':
                        if (!paused)
                        {
                            mpv_cmd(mpv_sock,
                                R"({"command": ["set_property", "pause", true]})""\n");
                            paused = true;
                            std::cout << "\033[A\r\033[K" << YELLOW << "  [ Paused ]" << RESET << std::flush;
                        }
                        break;
                    case 'c':
                        if (paused)
                        {
                            mpv_cmd(mpv_sock,
                                R"({"command": ["set_property", "pause", false]})""\n");
                            paused = false;
                            std::cout << "\033[A\r\033[K"<< '\n' << std::flush;
                        }
                        break;
                    case 'l':
                        mpv_cmd(mpv_sock, R"({"command": ["seek", 5]})""\n");
                        std::cout << "\033[A\r\033[K" << std::flush;
                        break;
                    case 'r':
                        mpv_cmd(mpv_sock, R"({"command": ["seek", -5]})""\n");
                        std::cout << "\033[A\r\033[K" << std::flush;
                        break;
                    case 'u':
                        mpv_cmd(mpv_sock, R"({"command": ["add", "volume", 5]})""\n");
                        std::cout << "\033[A\r\033[K" << std::flush;
                        break;
                    case 'd':
                        mpv_cmd(mpv_sock, R"({"command": ["add", "volume", -5]})""\n");
                        std::cout << "\033[A\r\033[K" << std::flush;
                        break;
                    default:
                        break;
                }
            }

            /* -------timer tick -> redraw-------- */
            if (e.filter == EVFILT_TIMER && !paused)
            {
                const double elapsed = mpv_get_elapsed(mpv_sock);
                if (elapsed > 0.0)
                    draw(elapsed, duration);
            }
        }
    }
}
#endif
std::string base(const std::string& path)
{
    if (path.empty()) return "";
    
    const auto pos = path.rfind('/');
    if (pos == std::string::npos) return path.substr(0);

    return path.substr(pos + 1);

}

void seq_play(const std::vector<std::string>& list)
{
    std::cout << SOFT_PURPLE << "\n========== Playing Sequentially ==========\n";

    for (size_t i = 0; i < list.size(); i++)
    {
        std::cout << "   " << std::endl;
        std::string name = base(list[i]);
        std::cout << '\n' << CYAN << "Playing [" << i + 1 << "] " << name << RESET << '\n' << std::endl;
        play(list[i]);
    }
}


void rand_play(const std::vector<std::string>& list)
{
    auto file_list = list;

    const unsigned seed = std::chrono::system_clock::now()
                        .time_since_epoch().count();

    std::shuffle(file_list.begin(), file_list.end(),
                 std::default_random_engine(seed));

    std::cout << SOFT_PURPLE << "\n========== Playing Randomly ==========\n";

    for (size_t i = 0; i < file_list.size(); i++)
    {
        std::cout << "   " << std::endl;
        std::string name = base(file_list[i]);
        std::cout << '\n'<< CYAN << "Playing [" << i + 1 << "] " << name << RESET << '\n' << std::endl;
        play(file_list[i]);
    }
}


int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);

    auto help = [&]() 
    {
        std::cout <<
        BOLD << "CLI Music Player\n" << RESET <<
        "Usage:\n"
        "  " << argv[0] << " <music_dir> [--seq | --rand]\n\n"
        "Example:\n"
        " " << argv[0] << "\"/Users/xuyan/Music --seq\n\n"
        "Options:\n"
        "  --seq     Play sequentially (default)\n"
        "  --rand    Play randomly\n"
        "  -h,--help Show this help message\n\n"
        "Commands:\n"
        "[s]+Enter: Pause \n"
        "[c]+Enter: Continue\n"
        "[n]+Enter: Next\n"
        "[q]+Enter: Quit\n\n";
    };

    if (argc == 1) 
    {
        help();
        return 0;
    }

    if (argc == 2 &&
       (std::string(argv[1]) == "-h" ||
        std::string(argv[1]) == "--help"))
    {
        help();
        return 0;
    }

    if (argc == 2)
    {
        const std::string path{argv[1]};
        if (exists(path) && is_music(path)) play(path);
    }

    std::string dir;
    bool random_mode = false;

    /* Handle cli parameter */
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "--rand")
            random_mode = true;
        else if (arg == "--seq")
            random_mode = false;
        else if (arg == "-h" || arg == "--help")
        {
            help();
            return 0;
        }
        else if (dir.empty())
            dir = arg;
        else
        {
            std::cerr << RED << "Unknown argument: " << arg << RESET << "\n";
            return 1;
        }
    }

    if (dir.empty())
    {
        std::cerr << RED << "Music directory required.\n" << RESET;
        help();
        return 1;
    }
    try
    {
        std::vector<std::string> list;
        const int total = scan(dir, list);

        if (total == 0) 
        {
            std::cout << YELLOW << "No music files found.\n" << RESET;
            return 0;
        }

        show_logo();

        if (random_mode)
            rand_play(list);
        else
            seq_play(list);

    }
    catch (const std::exception& e)
    {
        std::cerr << RED << "Error: " << e.what() << RESET << "\n";
        return 1;
    }

    return 0;
}
