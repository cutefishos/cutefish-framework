// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/networkmanager/wifi_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iconv.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <span>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include "base/locale.h"
#include "framework_i18n.h"

using cutefish::framework::tr;

namespace cutefish::networkmanager::wifi {

// SSIDs set before UTF-8 took over are in the legacy charset of the router
// owner's language. The code pages most setup pages wrote in come before
// the ISO and KOI8 sets, and wider supersets before what they contain.
struct LegacyCharsets
{
    std::vector<const char*> languages;
    std::vector<const char*> charsets;
};

static const LegacyCharsets legacyCharsets[] = {
    {{"zh_cn", "zh_sg"}, {"GB18030"}},
    {{"zh_tw", "zh_hk", "zh_mo"}, {"BIG5-HKSCS", "EUC-TW"}},
    {{"ja"}, {"CP932", "EUC-JP", "ISO-2022-JP"}},
    {{"ko"}, {"CP949", "ISO-2022-KR", "JOHAB"}},
    {{"uk"}, {"WINDOWS-1251", "KOI8-U"}},
    {{"ru", "be", "bg", "mk", "sr"}, {"WINDOWS-1251", "KOI8-R", "ISO-8859-5"}},
    {{"cs", "hr", "hu", "pl", "ro", "sh", "sk", "sl"}, {"WINDOWS-1250", "ISO-8859-2"}},
    {{"et", "lt", "lv"}, {"WINDOWS-1257", "ISO-8859-13"}},
    {{"el"}, {"WINDOWS-1253", "ISO-8859-7"}},
    {{"he", "iw"}, {"WINDOWS-1255", "ISO-8859-8"}},
    {{"ar"}, {"WINDOWS-1256", "ISO-8859-6"}},
    {{"th"}, {"WINDOWS-874"}},
    {{"tr"}, {"WINDOWS-1254", "ISO-8859-9"}},
};

// By language and country first, then by language alone.
static std::vector<const char*> charsetsFor(const std::string& language)
{
    std::string name = language.empty() ? base::Locale::system().name() : language;
    name = name.substr(0, name.find_first_of(".@"));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });

    for (const std::string& key : {name, name.substr(0, 2)}) {
        for (const LegacyCharsets& entry : legacyCharsets) {
            if (std::find(entry.languages.begin(), entry.languages.end(), key) != entry.languages.end())
                return entry.charsets;
        }
    }
    return {};
}

static bool decode(const std::string& bytes, const char* encoding, std::string* text)
{
    iconv_t converter = iconv_open("UTF-8", encoding);
    if (converter == iconv_t(-1))
        return false;

    std::string output(bytes.size() * 4 + 4, '\0');
    char* in = const_cast<char*>(bytes.data());
    size_t inLeft = bytes.size();
    char* out = output.data();
    size_t outLeft = output.size();
    const size_t result = iconv(converter, &in, &inLeft, &out, &outLeft);
    iconv_close(converter);
    if (result == size_t(-1) || inLeft > 0)
        return false;

    output.resize(output.size() - outLeft);
    *text = std::move(output);
    return true;
}

// The code points of valid UTF-8, or nothing when it isn't.
static bool decodeUtf8(const std::string& bytes, std::vector<char32_t>* codePoints)
{
    size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char lead = bytes[i];
        int length = 0;
        char32_t codePoint = 0;
        if (lead < 0x80) {
            length = 1;
            codePoint = lead;
        } else if ((lead & 0xe0) == 0xc0 && lead >= 0xc2) {
            length = 2;
            codePoint = lead & 0x1f;
        } else if ((lead & 0xf0) == 0xe0) {
            length = 3;
            codePoint = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0 && lead <= 0xf4) {
            length = 4;
            codePoint = lead & 0x07;
        } else {
            return false;
        }
        if (i + length > bytes.size())
            return false;
        for (int k = 1; k < length; ++k) {
            const unsigned char next = bytes[i + k];
            if ((next & 0xc0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        // Overlong forms, surrogates and values past Unicode.
        if ((length == 3 && codePoint < 0x800) || (length == 4 && (codePoint < 0x10000 || codePoint > 0x10ffff))
            || (codePoint >= 0xd800 && codePoint <= 0xdfff))
            return false;
        if (codePoints)
            codePoints->push_back(codePoint);
        i += length;
    }
    return true;
}

static void appendUtf8(std::string& out, char32_t c)
{
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3f));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    }
}

// A control character in a label would break the line it sits in.
static std::string printable(const std::string& utf8)
{
    std::vector<char32_t> codePoints;
    decodeUtf8(utf8, &codePoints);
    std::string text;
    for (const char32_t c : codePoints)
        appendUtf8(text, c < 0x20 || (c >= 0x7f && c <= 0x9f) ? char32_t(0xfffd) : c);
    return text;
}

// A cipher an access point lists, and the adapter capability it needs.
struct Cipher
{
    uint32_t flag;
    uint32_t capability;
};

static constexpr Cipher pairwiseCiphers[] = {
    {PairWep40, CapWep40}, {PairWep104, CapWep104}, {PairTkip, CapTkip}, {PairCcmp, CapCcmp}};
static constexpr Cipher groupCiphers[] = {
    {GroupWep40, CapWep40}, {GroupWep104, CapWep104}, {GroupTkip, CapTkip}, {GroupCcmp, CapCcmp}};

static bool anyUsable(uint32_t caps, uint32_t flags, std::span<const Cipher> ciphers)
{
    return std::any_of(ciphers.begin(), ciphers.end(), [caps, flags](const Cipher& cipher) {
        return (flags & cipher.flag) && (caps & cipher.capability);
    });
}

// The adapter can take part in what a WPA or RSN element lists. With static
// WEP the keys are fixed, so only a WEP group cipher has to match.
static bool ciphersUsable(uint32_t caps, uint32_t flags, bool staticWep)
{
    if (staticWep)
        return anyUsable(caps, flags, std::span(groupCiphers).first(2));
    return anyUsable(caps, flags, pairwiseCiphers) && anyUsable(caps, flags, groupCiphers);
}

// A pre-shared key needs TKIP or CCMP as the pairwise cipher.
static bool pskUsable(uint32_t caps, uint32_t flags)
{
    return anyUsable(caps, flags, std::span(pairwiseCiphers).last(2));
}

// Whether the adapter and an access point in range can use `type` together.
static bool usable(SecurityType type, uint32_t caps, bool adhoc, uint32_t apFlags, uint32_t wpa, uint32_t rsn)
{
    const bool privacy = apFlags & ApPrivacy;
    const bool wpaCapable = caps & CapWpa;
    const bool rsnCapable = caps & CapRsn;
    const bool rsnCcmp = (rsn & PairCcmp) && (caps & CapCcmp);

    switch (type) {
    case SecurityType::None:
        return !privacy && wpa == 0 && rsn == 0;
    case SecurityType::StaticWep:
    case SecurityType::Leap:
        // LEAP works on infrastructure networks only.
        if (!privacy || (adhoc && type == SecurityType::Leap))
            return false;
        return (wpa == 0 && rsn == 0) || ciphersUsable(caps, wpa, true) || ciphersUsable(caps, rsn, true);
    case SecurityType::DynamicWep:
        if (adhoc || rsn != 0 || !privacy)
            return false;
        // Some access points also send a WPA element that offers 802.1X.
        return wpa == 0 || ((wpa & KeyMgmt8021x) && ciphersUsable(caps, wpa, false));
    case SecurityType::WpaPsk:
        return !adhoc && wpaCapable && (wpa & KeyMgmtPsk) && pskUsable(caps, wpa);
    case SecurityType::Wpa2Psk:
        // An ad-hoc RSN network has no key management to advertise.
        if (adhoc)
            return rsnCapable && (caps & CapIbssRsn) && rsnCcmp;
        return rsnCapable && (rsn & KeyMgmtPsk) && pskUsable(caps, rsn);
    case SecurityType::WpaEap:
        return !adhoc && wpaCapable && (wpa & KeyMgmt8021x) && ciphersUsable(caps, wpa, false);
    case SecurityType::Wpa2Eap:
        return !adhoc && rsnCapable && (rsn & KeyMgmt8021x) && ciphersUsable(caps, rsn, false);
    case SecurityType::Sae:
        return !adhoc && rsnCapable && (rsn & KeyMgmtSae) && rsnCcmp;
    case SecurityType::Owe:
        return !adhoc && rsnCapable && (rsn & (KeyMgmtOwe | KeyMgmtOweTm));
    case SecurityType::Wpa3SuiteB192:
        return !adhoc && rsnCapable && (rsn & KeyMgmtEapSuiteB192);
    default:
        return false;
    }
}

// What the access point advertises, for devices that report no capabilities.
static SecurityType advertised(uint32_t apFlags, uint32_t wpa, uint32_t rsn)
{
    if (rsn & KeyMgmtEapSuiteB192)
        return SecurityType::Wpa3SuiteB192;
    if (rsn & KeyMgmtSae)
        return SecurityType::Sae;
    if (rsn & KeyMgmt8021x)
        return SecurityType::Wpa2Eap;
    if (rsn & KeyMgmtPsk)
        return SecurityType::Wpa2Psk;
    if (wpa & KeyMgmt8021x)
        return SecurityType::WpaEap;
    if (wpa & KeyMgmtPsk)
        return SecurityType::WpaPsk;
    if (rsn & (KeyMgmtOwe | KeyMgmtOweTm))
        return SecurityType::Owe;
    if (apFlags & ApPrivacy)
        return SecurityType::StaticWep;
    return SecurityType::None;
}

std::string ssidLabel(const std::string& ssid, const std::string& language)
{
    const std::string bytes = ssidKey(ssid);
    if (decodeUtf8(bytes, nullptr))
        return printable(bytes);

    for (const char* encoding : charsetsFor(language)) {
        std::string decoded;
        if (decode(bytes, encoding, &decoded))
            return printable(decoded);
    }

    std::string latin1;
    for (const unsigned char c : bytes)
        appendUtf8(latin1, c);
    return printable(latin1);
}

bool ssidHidden(const std::string& ssid)
{
    if (ssid == " ")
        return true;
    return std::all_of(ssid.begin(), ssid.end(), [](char c) { return c == '\0'; });
}

std::string ssidKey(const std::string& ssid)
{
    if (!ssid.empty() && ssid.back() == '\0')
        return ssid.substr(0, ssid.size() - 1);
    return ssid;
}

std::string ssidBytes(const std::string& name)
{
    if (name.size() <= 32)
        return name;
    size_t size = 32;
    while (size > 0 && (static_cast<unsigned char>(name[size]) & 0xc0) == 0x80)
        --size;
    return name.substr(0, size);
}

SecurityType security(uint32_t deviceCapabilities, uint32_t apFlags, uint32_t wpaFlags, uint32_t rsnFlags,
                      uint32_t mode)
{
    // Security level first, then how common each is.
    static const SecurityType order[] = {
        SecurityType::Wpa3SuiteB192, SecurityType::Sae,       SecurityType::Wpa2Eap,    SecurityType::Wpa2Psk,
        SecurityType::WpaEap,        SecurityType::WpaPsk,    SecurityType::StaticWep,  SecurityType::DynamicWep,
        SecurityType::Leap,          SecurityType::Owe,       SecurityType::None,
    };

    const bool adhoc = mode == ModeAdhoc;
    for (const auto type : order) {
        if (usable(type, deviceCapabilities, adhoc, apFlags, wpaFlags, rsnFlags))
            return type;
    }
    return advertised(apFlags, wpaFlags, rsnFlags);
}

bool enterprise(SecurityType type)
{
    return type == SecurityType::DynamicWep || type == SecurityType::Leap || type == SecurityType::WpaEap
           || type == SecurityType::Wpa2Eap || type == SecurityType::Wpa3SuiteB192;
}

bool needsPassword(SecurityType type)
{
    return type == SecurityType::StaticWep || type == SecurityType::WpaPsk || type == SecurityType::Wpa2Psk
           || type == SecurityType::Sae;
}

std::string securityLabel(SecurityType type)
{
    switch (type) {
    case SecurityType::None:
        // TRANSLATORS: a Wi-Fi network's security type.
        return tr("Insecure");
    case SecurityType::StaticWep:
        return "WEP";
    case SecurityType::Leap:
        return "LEAP";
    case SecurityType::DynamicWep:
        return tr("Dynamic WEP");
    case SecurityType::WpaPsk:
        return "WPA-PSK";
    case SecurityType::WpaEap:
        return "WPA-EAP";
    case SecurityType::Wpa2Psk:
        return "WPA2-PSK";
    case SecurityType::Wpa2Eap:
        return "WPA2-EAP";
    case SecurityType::Sae:
        return "WPA3-SAE";
    case SecurityType::Wpa3SuiteB192:
        return "WPA3-EAP-Suite-B-192";
    case SecurityType::Owe:
        return "OWE";
    default:
        // TRANSLATORS: a Wi-Fi network's security type.
        return tr("Unknown");
    }
}

base::dbus::VariantMap passwordEap(const std::string& identity, const std::string& password, uint32_t passwordFlags)
{
    return {{"eap", base::dbus::Variant(std::vector<std::string>{"peap", "ttls"})},
            {"phase2-auth", base::dbus::Variant(std::string("mschapv2"))},
            {"identity", base::dbus::Variant(identity)},
            {"password", base::dbus::Variant(password)},
            {"password-flags", base::dbus::Variant(passwordFlags)}};
}

int strengthStep(int strength)
{
    if (strength <= 0)
        return 0;
    if (strength <= 33)
        return 33;
    if (strength <= 66)
        return 66;
    return 100;
}

std::string strengthIcon(int strength)
{
    const int step = strengthStep(strength);
    return "network-wireless-connected-" + std::string(step < 10 ? "0" : "") + std::to_string(step);
}

bool pskValid(const std::string& psk)
{
    if (psk.size() == 64)
        return std::all_of(psk.begin(), psk.end(), [](unsigned char c) { return std::isxdigit(c); });
    return psk.size() >= 8 && psk.size() <= 63;
}

std::string generatedPassword()
{
    // nmcli's alphabet, without the characters that read like others.
    static const std::string alphabet = "234679ACDEFGHJKLMNPRTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::string password;
    while (password.size() < 12) {
        unsigned char random[16];
        if (getrandom(random, sizeof(random), 0) != ssize_t(sizeof(random)))
            continue;
        for (const unsigned char byte : random) {
            // Rejection sampling keeps every character equally likely.
            const unsigned limit = 256 - 256 % alphabet.size();
            if (byte < limit && password.size() < 12)
                password += alphabet[byte % alphabet.size()];
        }
    }
    return password;
}

WifiStandard standardFromInformationElements(std::span<const uint8_t> ies, uint32_t frequencyMHz)
{
    bool hasEht = false;
    bool hasHe = false;
    bool hasVht = false;
    bool hasHt = false;
    bool hasErp = false;
    bool hasOfdmRate = false;
    bool hasAnyRate = false;

    size_t i = 0;
    while (i + 2 <= ies.size()) {
        const uint8_t id = ies[i];
        const uint8_t len = ies[i + 1];
        if (i + 2 + len > ies.size())
            break;
        const std::span<const uint8_t> data = ies.subspan(i + 2, len);
        i += 2 + len;

        switch (id) {
        case 255: // Extension element
            if (!data.empty()) {
                if (data[0] == 106 || data[0] == 108)
                    hasEht = true;
                else if (data[0] == 35 || data[0] == 36)
                    hasHe = true;
            }
            break;
        case 191: // VHT Capabilities
        case 192: // VHT Operation
            hasVht = true;
            break;
        case 45: // HT Capabilities
        case 61: // HT Operation
            hasHt = true;
            break;
        case 42: // ERP Information
            hasErp = true;
            break;
        case 1:  // Supported Rates
        case 50: // Extended Supported Rates
            for (const uint8_t entry : data) {
                const uint8_t rate = entry & 0x7f;
                // Values above 54 Mbps (108 * 500 kbps) are BSS membership selectors.
                if (rate == 0 || rate > 108)
                    continue;
                hasAnyRate = true;
                if (rate == 12 || rate == 18 || rate == 24 || rate == 36 || rate == 48 || rate == 72 || rate == 96
                    || rate == 108)
                    hasOfdmRate = true;
            }
            break;
        default:
            break;
        }
    }

    if (hasEht)
        return WifiStandard::Wifi7;
    if (hasHe)
        return frequencyMHz >= 5925 && frequencyMHz <= 7125 ? WifiStandard::Wifi6E : WifiStandard::Wifi6;
    if (hasVht)
        return WifiStandard::Wifi5;
    if (hasHt)
        return WifiStandard::Wifi4;
    if (frequencyMHz >= 4900 && frequencyMHz < 5925)
        return hasAnyRate ? WifiStandard::Ieee80211a : WifiStandard::Unknown;
    if (frequencyMHz >= 2400 && frequencyMHz < 2500) {
        if (hasErp || hasOfdmRate)
            return WifiStandard::Ieee80211g;
        if (hasAnyRate)
            return WifiStandard::Ieee80211b;
    }
    return WifiStandard::Unknown;
}

std::string standardLabel(WifiStandard standard)
{
    switch (standard) {
    case WifiStandard::Wifi7:
        return "Wi-Fi 7 (802.11be)";
    case WifiStandard::Wifi6E:
        return "Wi-Fi 6E (802.11ax)";
    case WifiStandard::Wifi6:
        return "Wi-Fi 6 (802.11ax)";
    case WifiStandard::Wifi5:
        return "Wi-Fi 5 (802.11ac)";
    case WifiStandard::Wifi4:
        return "Wi-Fi 4 (802.11n)";
    case WifiStandard::Ieee80211g:
        return "802.11g";
    case WifiStandard::Ieee80211a:
        return "802.11a";
    case WifiStandard::Ieee80211b:
        return "802.11b";
    default:
        return {};
    }
}

namespace {

class UniqueFd
{
public:
    explicit UniqueFd(int fd)
        : m_fd(fd)
    {
    }
    ~UniqueFd()
    {
        if (m_fd >= 0)
            close(m_fd);
    }
    int get() const { return m_fd; }

private:
    int m_fd;
};

template<typename Fn>
void forEachAttr(const uint8_t* data, size_t len, Fn&& fn)
{
    while (len >= sizeof(nlattr)) {
        nlattr attr{};
        std::memcpy(&attr, data, sizeof(attr));
        if (attr.nla_len < sizeof(nlattr) || attr.nla_len > len)
            break;
        const size_t payloadLen = attr.nla_len - NLA_HDRLEN;
        fn(uint16_t(attr.nla_type & NLA_TYPE_MASK), std::span<const uint8_t>(data + NLA_HDRLEN, payloadLen));
        const size_t step = NLA_ALIGN(attr.nla_len);
        if (step > len)
            break;
        data += step;
        len -= step;
    }
}

uint16_t resolveNl80211Family(int fd)
{
    static constexpr char name[] = "nl80211";
    const size_t attrLen = NLA_HDRLEN + sizeof(name);
    const size_t msgLen = NLMSG_HDRLEN + GENL_HDRLEN + NLA_ALIGN(attrLen);
    std::vector<uint8_t> request(msgLen, 0);

    auto* nl = reinterpret_cast<nlmsghdr*>(request.data());
    nl->nlmsg_len = uint32_t(msgLen);
    nl->nlmsg_type = GENL_ID_CTRL;
    nl->nlmsg_flags = NLM_F_REQUEST;
    nl->nlmsg_seq = 1;

    auto* genl = reinterpret_cast<genlmsghdr*>(request.data() + NLMSG_HDRLEN);
    genl->cmd = CTRL_CMD_GETFAMILY;
    genl->version = 1;

    auto* attr = reinterpret_cast<nlattr*>(request.data() + NLMSG_HDRLEN + GENL_HDRLEN);
    attr->nla_len = uint16_t(attrLen);
    attr->nla_type = CTRL_ATTR_FAMILY_NAME;
    std::memcpy(request.data() + NLMSG_HDRLEN + GENL_HDRLEN + NLA_HDRLEN, name, sizeof(name));

    if (send(fd, request.data(), request.size(), 0) < 0)
        return 0;

    std::vector<uint8_t> reply(8192);
    const ssize_t received = recv(fd, reply.data(), reply.size(), 0);
    if (received < ssize_t(NLMSG_HDRLEN + GENL_HDRLEN))
        return 0;

    uint16_t familyId = 0;
    int remaining = int(received);
    const auto* header = reinterpret_cast<const nlmsghdr*>(reply.data());
    while (NLMSG_OK(header, remaining)) {
        if (header->nlmsg_type == NLMSG_ERROR || header->nlmsg_type == NLMSG_DONE)
            break;
        if (header->nlmsg_len > NLMSG_HDRLEN + GENL_HDRLEN) {
            const uint8_t* attrs = reinterpret_cast<const uint8_t*>(header) + NLMSG_HDRLEN + GENL_HDRLEN;
            const size_t attrsLen = header->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
            forEachAttr(attrs, attrsLen, [&](uint16_t type, std::span<const uint8_t> payload) {
                if (type == CTRL_ATTR_FAMILY_ID && payload.size() >= sizeof(uint16_t))
                    std::memcpy(&familyId, payload.data(), sizeof(familyId));
            });
        }
        header = NLMSG_NEXT(header, remaining);
    }
    return familyId;
}

} // namespace

std::map<std::string, WifiStandard> scanStandards(const std::string& interfaceName)
{
    std::map<std::string, WifiStandard> result;
    if (interfaceName.empty())
        return result;

    const uint32_t ifindex = if_nametoindex(interfaceName.c_str());
    if (ifindex == 0)
        return result;

    UniqueFd fd(socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC));
    if (fd.get() < 0)
        return result;

    const timeval timeout{.tv_sec = 0, .tv_usec = 200000};
    setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_nl local{};
    local.nl_family = AF_NETLINK;
    if (bind(fd.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
        return result;

    const uint16_t familyId = resolveNl80211Family(fd.get());
    if (familyId == 0)
        return result;

    const size_t attrLen = NLA_HDRLEN + sizeof(ifindex);
    const size_t msgLen = NLMSG_HDRLEN + GENL_HDRLEN + NLA_ALIGN(attrLen);
    std::vector<uint8_t> request(msgLen, 0);

    auto* nl = reinterpret_cast<nlmsghdr*>(request.data());
    nl->nlmsg_len = uint32_t(msgLen);
    nl->nlmsg_type = familyId;
    nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nl->nlmsg_seq = 2;

    auto* genl = reinterpret_cast<genlmsghdr*>(request.data() + NLMSG_HDRLEN);
    genl->cmd = NL80211_CMD_GET_SCAN;
    genl->version = 0;

    auto* attr = reinterpret_cast<nlattr*>(request.data() + NLMSG_HDRLEN + GENL_HDRLEN);
    attr->nla_len = uint16_t(attrLen);
    attr->nla_type = NL80211_ATTR_IFINDEX;
    std::memcpy(request.data() + NLMSG_HDRLEN + GENL_HDRLEN + NLA_HDRLEN, &ifindex, sizeof(ifindex));

    if (send(fd.get(), request.data(), request.size(), 0) < 0)
        return result;

    std::vector<uint8_t> buffer(65536);
    bool done = false;
    while (!done) {
        const ssize_t received = recv(fd.get(), buffer.data(), buffer.size(), 0);
        if (received <= 0)
            break;
        int remaining = int(received);
        const auto* header = reinterpret_cast<const nlmsghdr*>(buffer.data());
        while (NLMSG_OK(header, remaining)) {
            if (header->nlmsg_type == NLMSG_DONE || header->nlmsg_type == NLMSG_ERROR) {
                done = true;
                break;
            }
            if (header->nlmsg_type == familyId && header->nlmsg_len > NLMSG_HDRLEN + GENL_HDRLEN) {
                const uint8_t* attrs = reinterpret_cast<const uint8_t*>(header) + NLMSG_HDRLEN + GENL_HDRLEN;
                const size_t attrsLen = header->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
                forEachAttr(attrs, attrsLen, [&](uint16_t type, std::span<const uint8_t> payload) {
                    if (type != NL80211_ATTR_BSS)
                        return;
                    std::string bssid;
                    uint32_t freq = 0;
                    std::span<const uint8_t> infoElements;
                    std::span<const uint8_t> beaconIes;
                    forEachAttr(payload.data(), payload.size(),
                                [&](uint16_t bssType, std::span<const uint8_t> bssData) {
                                    if (bssType == NL80211_BSS_BSSID && bssData.size() == 6) {
                                        char formatted[18];
                                        std::snprintf(formatted, sizeof(formatted), "%02x:%02x:%02x:%02x:%02x:%02x",
                                                      bssData[0], bssData[1], bssData[2], bssData[3], bssData[4],
                                                      bssData[5]);
                                        bssid = formatted;
                                    } else if (bssType == NL80211_BSS_FREQUENCY && bssData.size() >= sizeof(uint32_t)) {
                                        std::memcpy(&freq, bssData.data(), sizeof(freq));
                                    } else if (bssType == NL80211_BSS_INFORMATION_ELEMENTS) {
                                        infoElements = bssData;
                                    } else if (bssType == NL80211_BSS_BEACON_IES) {
                                        beaconIes = bssData;
                                    }
                                });
                    if (bssid.empty())
                        return;
                    const WifiStandard standard = std::max(standardFromInformationElements(infoElements, freq),
                                                           standardFromInformationElements(beaconIes, freq));
                    if (standard != WifiStandard::Unknown)
                        result[bssid] = std::max(result[bssid], standard);
                });
            }
            header = NLMSG_NEXT(header, remaining);
        }
    }
    return result;
}

} // namespace cutefish::networkmanager::wifi
