#include <jni.h>
#include <android/log.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <dirent.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>

#define TAG "VPNHideTest"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static const char* VPN_PREFIXES[] = {"tun", "wg", "ppp", "tap", "ipsec", "xfrm"};
static const int NUM_PREFIXES = sizeof(VPN_PREFIXES) / sizeof(VPN_PREFIXES[0]);

static bool is_vpn_iface(const char* name) {
    for (int i = 0; i < NUM_PREFIXES; i++) {
        if (strncmp(name, VPN_PREFIXES[i], strlen(VPN_PREFIXES[i])) == 0) {
            return true;
        }
    }
    if (strstr(name, "vpn") || strstr(name, "VPN")) return true;
    return false;
}

static jstring to_jstring(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

// 1. ioctl SIOCGIFFLAGS on tun0
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkIoctlSiocgifflags(JNIEnv* env, jobject) {
    LOGI("=== CHECK: ioctl SIOCGIFFLAGS on tun0 ===");
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::string r = "FAIL: cannot create socket: " + std::string(strerror(errno));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    struct ifreq ifr{};
    strncpy(ifr.ifr_name, "tun0", IFNAMSIZ - 1);

    int ret = ioctl(fd, SIOCGIFFLAGS, &ifr);
    int err = errno;
    close(fd);

    std::string result;
    if (ret < 0) {
        if (err == ENODEV) {
            result = "PASS: ioctl(tun0, SIOCGIFFLAGS) returned ENODEV — interface not visible";
        } else if (err == ENXIO) {
            result = "PASS: ioctl(tun0, SIOCGIFFLAGS) returned ENXIO — interface not visible";
        } else {
            result = "FAIL: ioctl returned error " + std::to_string(err) + " (" + strerror(err) + ")";
        }
    } else {
        result = "FAIL: tun0 is visible! flags=0x" + std::to_string(ifr.ifr_flags) +
                 " (IFF_UP=" + std::to_string(!!(ifr.ifr_flags & IFF_UP)) +
                 ", IFF_RUNNING=" + std::to_string(!!(ifr.ifr_flags & IFF_RUNNING)) + ")";
    }
    LOGI("RESULT: %s", result.c_str());
    return to_jstring(env, result);
}

// 2. ioctl SIOCGIFCONF - enumerate interfaces
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkIoctlSiocgifconf(JNIEnv* env, jobject) {
    LOGI("=== CHECK: ioctl SIOCGIFCONF enumeration ===");
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::string r = "FAIL: cannot create socket: " + std::string(strerror(errno));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    char buf[4096];
    struct ifconf ifc{};
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
        int err = errno;
        close(fd);
        std::string r = "FAIL: ioctl error: " + std::string(strerror(err));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }
    close(fd);

    struct ifreq* it = ifc.ifc_req;
    int count = ifc.ifc_len / sizeof(struct ifreq);
    std::vector<std::string> all_ifaces;
    std::vector<std::string> vpn_found;

    for (int i = 0; i < count; i++) {
        const char* name = it[i].ifr_name;
        all_ifaces.push_back(name);
        LOGI("  SIOCGIFCONF: interface '%s'", name);
        if (is_vpn_iface(name)) {
            vpn_found.push_back(name);
        }
    }

    std::string all_list;
    for (auto& n : all_ifaces) { if (!all_list.empty()) all_list += ", "; all_list += n; }

    std::string result;
    if (vpn_found.empty()) {
        result = "PASS: " + std::to_string(count) + " interfaces visible: [" + all_list + "], none are VPN";
    } else {
        std::string vpn_list;
        for (auto& n : vpn_found) { if (!vpn_list.empty()) vpn_list += ", "; vpn_list += n; }
        result = "FAIL: VPN interfaces found: [" + vpn_list + "] in full list: [" + all_list + "]";
    }
    LOGI("RESULT: %s", result.c_str());
    return to_jstring(env, result);
}

// 3. getifaddrs
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkGetifaddrs(JNIEnv* env, jobject) {
    LOGI("=== CHECK: getifaddrs() enumeration ===");
    struct ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0) {
        std::string r = "FAIL: getifaddrs error: " + std::string(strerror(errno));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    std::vector<std::string> all_ifaces;
    std::vector<std::string> vpn_found;
    for (struct ifaddrs* ifa = addrs; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;
        // Deduplicate for display
        bool seen = false;
        for (auto& n : all_ifaces) if (n == ifa->ifa_name) { seen = true; break; }
        if (!seen) {
            all_ifaces.push_back(ifa->ifa_name);
            LOGI("  getifaddrs: interface '%s' (family=%d, flags=0x%x)",
                 ifa->ifa_name, ifa->ifa_addr ? ifa->ifa_addr->sa_family : -1, ifa->ifa_flags);
        }
        if (is_vpn_iface(ifa->ifa_name)) {
            bool dup = false;
            for (auto& n : vpn_found) if (n == ifa->ifa_name) { dup = true; break; }
            if (!dup) vpn_found.push_back(ifa->ifa_name);
        }
    }
    freeifaddrs(addrs);

    std::string all_list;
    for (auto& n : all_ifaces) { if (!all_list.empty()) all_list += ", "; all_list += n; }

    std::string result;
    if (vpn_found.empty()) {
        result = "PASS: " + std::to_string(all_ifaces.size()) + " unique interfaces: [" + all_list + "], none are VPN";
    } else {
        std::string vpn_list;
        for (auto& n : vpn_found) { if (!vpn_list.empty()) vpn_list += ", "; vpn_list += n; }
        result = "FAIL: VPN interfaces: [" + vpn_list + "] in full list: [" + all_list + "]";
    }
    LOGI("RESULT: %s", result.c_str());
    return to_jstring(env, result);
}

// Helper: read a proc file and return detailed info
static std::string check_proc_file(const char* path) {
    LOGI("=== CHECK: %s (native read) ===", path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) {
            std::string r = "PASS: access denied by SELinux (" + std::string(strerror(errno)) + ") — app cannot read " + path;
            LOGI("RESULT: %s", r.c_str());
            return r;
        }
        std::string r = "FAIL: cannot open " + std::string(path) + ": " + strerror(errno);
        LOGI("RESULT: %s", r.c_str());
        return r;
    }

    char buf[8192];
    std::string content;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        content += buf;
    }
    close(fd);

    int total_lines = 0;
    std::vector<std::string> vpn_lines;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        std::string line = content.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        total_lines++;

        LOGI("  %s line: %s", path, line.substr(0, 120).c_str());

        for (int i = 0; i < NUM_PREFIXES; i++) {
            if (line.find(VPN_PREFIXES[i]) != std::string::npos) {
                vpn_lines.push_back(line.substr(0, 80));
                break;
            }
        }
    }

    std::string result;
    if (vpn_lines.empty()) {
        result = "PASS: " + std::to_string(total_lines) + " lines in " + path + ", no VPN entries";
    } else {
        result = "FAIL: " + std::to_string(vpn_lines.size()) + " VPN lines in " + path + ":";
        for (auto& l : vpn_lines) result += "\n  " + l;
    }
    LOGI("RESULT: %s", result.c_str());
    return result;
}

// 4. /proc/net/route
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetRoute(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/route"));
}

// 5. /proc/net/if_inet6
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetIfInet6(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/if_inet6"));
}

// Helper: try to open and bind a netlink route socket.
// Returns fd on success, -1 on failure. Sets result string on SELinux denial.
static int open_netlink(std::string& err_result) {
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) {
            err_result = "PASS: netlink socket denied by SELinux (" + std::string(strerror(errno)) + ")";
        } else {
            err_result = "FAIL: cannot create netlink socket: " + std::string(strerror(errno));
        }
        return -1;
    }

    struct sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        int err = errno;
        close(fd);
        if (err == EACCES || err == EPERM) {
            err_result = "PASS: netlink bind denied by SELinux (" + std::string(strerror(err)) + ")";
        } else {
            err_result = "FAIL: bind error: " + std::string(strerror(err));
        }
        return -1;
    }
    return fd;
}

// 6. Netlink RTM_GETLINK
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkNetlinkGetlink(JNIEnv* env, jobject) {
    LOGI("=== CHECK: netlink RTM_GETLINK dump ===");
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        std::string r = "FAIL: cannot create netlink socket: " + std::string(strerror(errno));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    struct sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        int err = errno;
        close(fd);
        if (err == EACCES || err == EPERM) {
            std::string r = "PASS: netlink bind denied by SELinux (" + std::string(strerror(err)) + ") — app cannot enumerate interfaces";
            LOGI("RESULT: %s", r.c_str());
            return to_jstring(env, r);
        }
        std::string r = "FAIL: bind error: " + std::string(strerror(err));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    struct {
        struct nlmsghdr nlh;
        struct ifinfomsg ifm;
    } req{};

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.nlh.nlmsg_type = RTM_GETLINK;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 1;
    req.ifm.ifi_family = AF_UNSPEC;

    if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        int err = errno;
        close(fd);
        std::string r = "FAIL: send error: " + std::string(strerror(err));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    char buf[32768];
    std::vector<std::string> all_ifaces;
    std::vector<std::string> vpn_found;
    bool done = false;

    while (!done) {
        ssize_t len = recv(fd, buf, sizeof(buf), 0);
        if (len <= 0) break;

        for (struct nlmsghdr* nh = (struct nlmsghdr*)buf;
             NLMSG_OK(nh, (size_t)len);
             nh = NLMSG_NEXT(nh, len)) {

            if (nh->nlmsg_type == NLMSG_DONE) { done = true; break; }
            if (nh->nlmsg_type == NLMSG_ERROR) { done = true; break; }
            if (nh->nlmsg_type != RTM_NEWLINK) continue;

            struct ifinfomsg* ifi = (struct ifinfomsg*)NLMSG_DATA(nh);
            struct rtattr* rta = IFLA_RTA(ifi);
            int rta_len = IFLA_PAYLOAD(nh);

            while (RTA_OK(rta, rta_len)) {
                if (rta->rta_type == IFLA_IFNAME) {
                    const char* name = (const char*)RTA_DATA(rta);
                    all_ifaces.push_back(name);
                    LOGI("  netlink RTM_NEWLINK: interface '%s' (index=%d, flags=0x%x)",
                         name, ifi->ifi_index, ifi->ifi_flags);
                    if (is_vpn_iface(name)) {
                        vpn_found.push_back(name);
                    }
                }
                rta = RTA_NEXT(rta, rta_len);
            }
        }
    }
    close(fd);

    std::string all_list;
    for (auto& n : all_ifaces) { if (!all_list.empty()) all_list += ", "; all_list += n; }

    std::string result;
    if (vpn_found.empty()) {
        result = "PASS: " + std::to_string(all_ifaces.size()) + " interfaces via netlink: [" + all_list + "], none are VPN";
    } else {
        std::string vpn_list;
        for (auto& n : vpn_found) { if (!vpn_list.empty()) vpn_list += ", "; vpn_list += n; }
        result = "FAIL: VPN interfaces: [" + vpn_list + "] in netlink dump: [" + all_list + "]";
    }
    LOGI("RESULT: %s", result.c_str());
    return to_jstring(env, result);
}

// 7. netlink RTM_GETROUTE
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkNetlinkGetroute(JNIEnv* env, jobject) {
    LOGI("=== CHECK: netlink RTM_GETROUTE dump ===");
    std::string err;
    int fd = open_netlink(err);
    if (fd < 0) {
        LOGI("RESULT: %s", err.c_str());
        return to_jstring(env, err);
    }

    struct {
        struct nlmsghdr nlh;
        struct rtmsg rtm;
    } req{};
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type = RTM_GETROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 1;
    req.rtm.rtm_family = AF_UNSPEC;

    if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        int e = errno;
        close(fd);
        std::string r = "FAIL: send error: " + std::string(strerror(e));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    char buf[32768];
    std::vector<std::string> vpn_found;
    int total = 0;
    bool done = false;

    while (!done) {
        ssize_t len = recv(fd, buf, sizeof(buf), 0);
        if (len <= 0) break;
        for (struct nlmsghdr* nh = (struct nlmsghdr*)buf;
             NLMSG_OK(nh, (size_t)len); nh = NLMSG_NEXT(nh, len)) {
            if (nh->nlmsg_type == NLMSG_DONE) { done = true; break; }
            if (nh->nlmsg_type == NLMSG_ERROR) { done = true; break; }
            if (nh->nlmsg_type != RTM_NEWROUTE) continue;
            total++;
            struct rtmsg* rtm = (struct rtmsg*)NLMSG_DATA(nh);
            struct rtattr* rta = RTM_RTA(rtm);
            int rta_len = RTM_PAYLOAD(nh);
            while (RTA_OK(rta, rta_len)) {
                if (rta->rta_type == RTA_OIF) {
                    int ifindex = *(int*)RTA_DATA(rta);
                    char ifname[IFNAMSIZ];
                    if (if_indextoname(ifindex, ifname) && is_vpn_iface(ifname)) {
                        vpn_found.push_back(ifname);
                        LOGI("  RTM_GETROUTE: VPN route via '%s'", ifname);
                    }
                }
                rta = RTA_NEXT(rta, rta_len);
            }
        }
    }
    close(fd);

    std::string result;
    if (vpn_found.empty()) {
        result = "PASS: " + std::to_string(total) + " routes, no VPN";
    } else {
        std::string vl;
        for (auto& n : vpn_found) { if (!vl.empty()) vl += ", "; vl += n; }
        result = "FAIL: VPN routes via [" + vl + "]";
    }
    LOGI("RESULT: %s", result.c_str());
    return to_jstring(env, result);
}

// 8. /proc/net/ipv6_route
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetIpv6Route(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/ipv6_route"));
}

// 9. /proc/net/tcp
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetTcp(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/tcp"));
}

// 10. /proc/net/tcp6
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetTcp6(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/tcp6"));
}

// 11. /proc/net/udp
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetUdp(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/udp"));
}

// 12. /proc/net/udp6
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetUdp6(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/udp6"));
}

// 13. /proc/net/dev
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetDev(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/dev"));
}

// 14. /proc/net/fib_trie
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkProcNetFibTrie(JNIEnv* env, jobject) {
    return to_jstring(env, check_proc_file("/proc/net/fib_trie"));
}

// 15. /sys/class/net — check for VPN interface directories
extern "C" JNIEXPORT jstring JNICALL
Java_dev_okhsunrog_vpnhide_test_NativeChecks_checkSysClassNet(JNIEnv* env, jobject) {
    LOGI("=== CHECK: /sys/class/net/ directory ===");
    DIR* dir = opendir("/sys/class/net");
    if (!dir) {
        int err = errno;
        if (err == EACCES || err == EPERM) {
            std::string r = "PASS: access denied by SELinux (" + std::string(strerror(err)) + ")";
            LOGI("RESULT: %s", r.c_str());
            return to_jstring(env, r);
        }
        std::string r = "FAIL: cannot open /sys/class/net: " + std::string(strerror(err));
        LOGI("RESULT: %s", r.c_str());
        return to_jstring(env, r);
    }

    std::vector<std::string> all_ifaces;
    std::vector<std::string> vpn_found;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        all_ifaces.push_back(entry->d_name);
        LOGI("  /sys/class/net: '%s'", entry->d_name);
        if (is_vpn_iface(entry->d_name)) {
            vpn_found.push_back(entry->d_name);
        }
    }
    closedir(dir);

    std::string all_list;
    for (auto& n : all_ifaces) { if (!all_list.empty()) all_list += ", "; all_list += n; }

    std::string result;
    if (vpn_found.empty()) {
        result = "PASS: [" + all_list + "], no VPN";
    } else {
        std::string vl;
        for (auto& n : vpn_found) { if (!vl.empty()) vl += ", "; vl += n; }
        result = "FAIL: VPN interfaces [" + vl + "] in [" + all_list + "]";
    }
    LOGI("RESULT: %s", result.c_str());
    return to_jstring(env, result);
}
