// The desktop build's side of updates (ticket 1.10): reading a release from a
// folder (--release-folder) and the pretend installer. Uses the real laptop
// file system, but no internet.

#include <unity.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>

#include "app/Version.h"
#include "app/bot/Bot.h"
#include "app/updates/Sha256.h"
#include "app/updates/Updates.h"
#include "fakes/FakeTelegram.h"
#include "fakes/Fakes.h"
#include "platform/desktop/DesktopFirmwareInstaller.h"
#include "platform/desktop/DesktopNetwork.h"

namespace fs = std::filesystem;
using namespace fvb;
using namespace fvb::fakes;

static fs::path tempRoot;
static const std::string kFirmware(5000, 'F');

static void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

static std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A release folder in the format scripts/make-release.sh writes.
static void makeRelease(const std::string& version, const std::string& firmware) {
    writeFile(tempRoot / "release" / "firmware.bin", firmware);
    writeFile(tempRoot / "release" / "firmware.json",
              R"({"version": ")" + version + R"(", "notes": "A test", "size": )" + std::to_string(kFirmware.size()) +
                  R"(, "sha256": ")" + Sha256::of(kFirmware) + "\"}\n");
}

struct Box {
    FakeHardware hw;
    DesktopNetwork network;
    Storage storage{hw.files, hw.settings};
    telegram::TelegramClient client{hw.network, hw.files, hw.log, kTestBotToken};
    Bot bot{client, storage, hw.settings, hw.clock, hw.log};
    DesktopFirmwareInstaller installer{tempRoot / "firmware", hw.log};
    Updates updates{bot, client, storage, hw.settings, hw.clock, hw.log, network, installer,
                    ReleaseLocation::folder((tempRoot / "release").string())};
};

void setUp() {
    std::random_device rd;
    tempRoot = fs::temp_directory_path() / ("fvb-updates-test-" + std::to_string(rd()));
    fs::remove_all(tempRoot);
    setPretendFirmwareVersion("");
}

void tearDown() {
    fs::remove_all(tempRoot);
    setPretendFirmwareVersion("");
}

void test_no_release_folder_means_up_to_date() {
    Box box;
    const Updates::Result r = box.updates.check();
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.newer);
}

void test_reads_a_release_from_a_folder() {
    makeRelease("9.9.9", kFirmware);
    Box box;
    const Updates::Result r = box.updates.check();
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.newer);
    TEST_ASSERT_EQUAL_STRING("9.9.9", r.release.version.c_str());
    TEST_ASSERT_EQUAL_STRING("A test", r.release.notes.c_str());
}

void test_pretend_install_saves_the_firmware_and_version() {
    makeRelease("9.9.9", kFirmware);
    Box box;
    const Updates::Result r = box.updates.download(box.updates.check().release);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error.c_str());
    TEST_ASSERT_TRUE(readFile(tempRoot / "firmware" / "firmware.bin") == kFirmware);
    TEST_ASSERT_EQUAL_STRING("9.9.9", box.installer.installedVersion().c_str());
    TEST_ASSERT_FALSE(fs::exists(tempRoot / "firmware" / "download.part"));

    box.installer.forget();
    TEST_ASSERT_EQUAL_STRING("", box.installer.installedVersion().c_str());
}

void test_damaged_firmware_isnt_installed() {
    std::string damaged = kFirmware;
    damaged[100] = 'X';
    makeRelease("9.9.9", damaged);
    Box box;
    const Updates::Result r = box.updates.download(box.updates.check().release);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("", box.installer.installedVersion().c_str());
    TEST_ASSERT_FALSE(fs::exists(tempRoot / "firmware" / "firmware.bin"));
    TEST_ASSERT_FALSE(fs::exists(tempRoot / "firmware" / "download.part"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_release_folder_means_up_to_date);
    RUN_TEST(test_reads_a_release_from_a_folder);
    RUN_TEST(test_pretend_install_saves_the_firmware_and_version);
    RUN_TEST(test_damaged_firmware_isnt_installed);
    return UNITY_END();
}
