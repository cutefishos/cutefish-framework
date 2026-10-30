// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "cutefish/accounts/account_picture.h"

using namespace cutefish::accounts;
namespace fs = std::filesystem;

class AccountPictureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = fs::temp_directory_path() / ("account-picture-test-" + std::to_string(getpid()));
        fs::create_directories(m_directory / "nested");
        for (const char* name : {"night.jpg", "autumn.jpg", "hills.jpg"})
            std::ofstream(m_directory / name) << "x";
    }

    void TearDown() override { fs::remove_all(m_directory); }

    fs::path m_directory;
};

TEST_F(AccountPictureTest, ListsThePresetFilesSorted)
{
    const std::vector<std::string> pictures = presetPictures(m_directory.string());
    ASSERT_EQ(pictures.size(), 3u);
    EXPECT_EQ(pictures[0], (m_directory / "autumn.jpg").string());
    EXPECT_EQ(pictures[1], (m_directory / "hills.jpg").string());
    EXPECT_EQ(pictures[2], (m_directory / "night.jpg").string());
}

TEST_F(AccountPictureTest, PicksOneOfThePresets)
{
    const std::vector<std::string> pictures = presetPictures(m_directory.string());
    for (int i = 0; i < 20; ++i) {
        const std::string picked = randomPresetPicture(m_directory.string());
        EXPECT_NE(std::find(pictures.begin(), pictures.end(), picked), pictures.end());
    }
}

TEST_F(AccountPictureTest, NoPresetsPickNothing)
{
    EXPECT_TRUE(presetPictures((m_directory / "missing").string()).empty());
    EXPECT_EQ(randomPresetPicture((m_directory / "missing").string()), "");
}

TEST(AccountPictureColor, IsTheSameForOneUserName)
{
    EXPECT_EQ(pictureColor("alice"), pictureColor("alice"));
    EXPECT_EQ(pictureColor(""), pictureColor(""));
    EXPECT_LE(pictureColor("alice"), 0xFFFFFFu);
}
