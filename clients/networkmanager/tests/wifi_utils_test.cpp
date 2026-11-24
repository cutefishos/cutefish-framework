// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#include "cutefish/networkmanager/wifi_utils.h"

using namespace cutefish::networkmanager;

TEST(WifiUtils, SsidLabels)
{
    EXPECT_EQ(wifi::ssidLabel("Caf\xc3\xa9"), "Caf\xc3\xa9");
    // GB18030 bytes of two Han characters, which a zh_CN system tries after UTF-8.
    EXPECT_EQ(wifi::ssidLabel("\xd6\xd0\xce\xc4", "zh_CN"), "\xe4\xb8\xad\xe6\x96\x87");
    EXPECT_EQ(wifi::ssidLabel(std::string("Lab\0", 4)), "Lab");
    EXPECT_EQ(wifi::ssidLabel("a\nb"), "a\xef\xbf\xbd" "b");
    // Without a matching charset the bytes read as Latin-1.
    EXPECT_EQ(wifi::ssidLabel("\xe9t\xe9", "en_US"), "\xc3\xa9t\xc3\xa9");
}

TEST(WifiUtils, HiddenSsids)
{
    EXPECT_TRUE(wifi::ssidHidden(""));
    EXPECT_TRUE(wifi::ssidHidden(std::string("\0\0\0", 3)));
    EXPECT_TRUE(wifi::ssidHidden(" "));
    EXPECT_FALSE(wifi::ssidHidden("  "));
    EXPECT_FALSE(wifi::ssidHidden("Home"));
}

TEST(WifiUtils, Security)
{
    // WEP40, WEP104, TKIP, CCMP, WPA and RSN.
    const uint32_t caps = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20;
    const uint32_t ccmp = 0x8 | 0x80;
    EXPECT_EQ(wifi::security(caps, 0, 0, 0, 2), SecurityType::None);
    EXPECT_EQ(wifi::security(caps, 1, 0, 0, 2), SecurityType::StaticWep);
    EXPECT_EQ(wifi::security(caps, 1, 0, ccmp | 0x100, 2), SecurityType::Wpa2Psk);
    EXPECT_EQ(wifi::security(caps, 1, 0, ccmp | 0x100 | 0x400, 2), SecurityType::Sae);
    EXPECT_EQ(wifi::security(caps, 1, 0, ccmp | 0x200, 2), SecurityType::Wpa2Eap);
    EXPECT_EQ(wifi::security(caps, 0, 0, 0x1000, 2), SecurityType::Owe);
    EXPECT_EQ(wifi::security(caps, 1, 0x4 | 0x40 | 0x100, 0, 2), SecurityType::WpaPsk);
    // A device that reports no capabilities still gets a lock for a secured network.
    EXPECT_EQ(wifi::security(0, 1, 0, ccmp | 0x100, 2), SecurityType::Wpa2Psk);
}

TEST(WifiUtils, Secrets)
{
    EXPECT_TRUE(wifi::pskValid("12345678"));
    EXPECT_FALSE(wifi::pskValid("1234567"));
    EXPECT_TRUE(wifi::pskValid(std::string(64, 'a')));
    EXPECT_FALSE(wifi::pskValid(std::string(64, 'g')));
    EXPECT_EQ(wifi::generatedPassword().size(), 12u);
    EXPECT_TRUE(wifi::pskValid(wifi::generatedPassword()));
    EXPECT_NE(wifi::generatedPassword(), wifi::generatedPassword());
}

TEST(WifiUtils, Strength)
{
    EXPECT_EQ(wifi::strengthIcon(0), "network-wireless-connected-00");
    EXPECT_EQ(wifi::strengthIcon(33), "network-wireless-connected-33");
    EXPECT_EQ(wifi::strengthIcon(34), "network-wireless-connected-66");
    EXPECT_EQ(wifi::strengthIcon(90), "network-wireless-connected-100");
}

TEST(WifiUtils, SsidBytesCutOnACharacter)
{
    const std::string name = std::string(31, 'a') + "\xe4\xb8\xad";
    EXPECT_EQ(wifi::ssidBytes(name), std::string(31, 'a'));
    EXPECT_EQ(wifi::ssidBytes("Home"), "Home");
}

TEST(WifiUtils, StandardFromInformationElements)
{
    // Wi-Fi 7 (802.11be): HT + VHT + HE + EHT capabilities.
    const uint8_t ehtIes[] = {45, 2, 0, 0, 191, 2, 0, 0, 255, 2, 35, 0, 255, 2, 108, 0};
    EXPECT_EQ(wifi::standardFromInformationElements(ehtIes, 5180), WifiStandard::Wifi7);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Wifi7), "Wi-Fi 7 (802.11be)");

    // Wi-Fi 6 (802.11ax) on 5 GHz and Wi-Fi 6E on 6 GHz.
    const uint8_t heIes[] = {45, 2, 0, 0, 191, 2, 0, 0, 255, 2, 35, 0};
    EXPECT_EQ(wifi::standardFromInformationElements(heIes, 5180), WifiStandard::Wifi6);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Wifi6), "Wi-Fi 6 (802.11ax)");
    EXPECT_EQ(wifi::standardFromInformationElements(heIes, 5955), WifiStandard::Wifi6E);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Wifi6E), "Wi-Fi 6E (802.11ax)");

    // Wi-Fi 5 (802.11ac) and Wi-Fi 4 (802.11n).
    const uint8_t vhtIes[] = {45, 2, 0, 0, 191, 2, 0, 0};
    EXPECT_EQ(wifi::standardFromInformationElements(vhtIes, 5180), WifiStandard::Wifi5);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Wifi5), "Wi-Fi 5 (802.11ac)");

    const uint8_t htIes[] = {45, 2, 0, 0};
    EXPECT_EQ(wifi::standardFromInformationElements(htIes, 2412), WifiStandard::Wifi4);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Wifi4), "Wi-Fi 4 (802.11n)");

    // Legacy 802.11a/g/b.
    const uint8_t ratesG[] = {1, 4, 0x82, 0x84, 0x8b, 0x96, 50, 2, 12, 108};
    EXPECT_EQ(wifi::standardFromInformationElements(ratesG, 2412), WifiStandard::Ieee80211g);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Ieee80211g), "802.11g");
    EXPECT_EQ(wifi::standardFromInformationElements(ratesG, 5180), WifiStandard::Ieee80211a);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Ieee80211a), "802.11a");

    const uint8_t ratesB[] = {1, 4, 0x82, 0x84, 0x8b, 0x96};
    EXPECT_EQ(wifi::standardFromInformationElements(ratesB, 2412), WifiStandard::Ieee80211b);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Ieee80211b), "802.11b");

    EXPECT_EQ(wifi::standardFromInformationElements({}, 2412), WifiStandard::Unknown);
    EXPECT_EQ(wifi::standardLabel(WifiStandard::Unknown), "");
}
