namespace {
struct TemporaryVitaFs {
    fs::path root = fs::temp_directory_path() / fs::unique_path("emucore-user-%%%%-%%%%-%%%%");

    ~TemporaryVitaFs() {
        boost::system::error_code error;
        fs::remove_all(root, error);
    }

    void write_profile(const std::string &id, const std::string &xml) const {
        const fs::path profile = root / "ux0" / "user" / id;
        fs::create_directories(profile);
        fs::ofstream file(profile / "user.xml");
        file << xml;
    }
};
}

TEST(AppUser, fresh_default_profile_uses_EmuCore) {
    TemporaryVitaFs temp;
    EmuEnvState env;
    env.vita_fs_path = temp.root;
    env.cfg.config_path = temp.root / "config.yml";

    ASSERT_TRUE(app::ensure_current_user(env));
    ASSERT_EQ(env.app.user_list.users.size(), 1);
    EXPECT_EQ(env.cfg.user_id, "00");
    EXPECT_EQ(env.io.user_id, "00");
    EXPECT_EQ(env.io.user_name, "EmuCore");
    EXPECT_EQ(env.app.user_list.users.at("00").name, "EmuCore");
}

TEST(AppUser, legacy_generated_default_rename_preserves_id_and_savedata) {
    TemporaryVitaFs temp;
    temp.write_profile("00", R"(<user id="00" name="Vita3K"><theme use-background="true"><content-id>default</content-id></theme><start-screen type="default"><path></path></start-screen><backgrounds/></user>)");
    const fs::path save_file = temp.root / "ux0" / "user" / "00" / "savedata" / "PCSE00000" / "data.bin";
    fs::create_directories(save_file.parent_path());
    fs::ofstream(save_file) << "save-data";

    EmuEnvState env;
    env.vita_fs_path = temp.root;
    env.cfg.user_id = "00";
    app::load_users(env);

    ASSERT_TRUE(app::ensure_current_user(env));
    EXPECT_EQ(env.cfg.user_id, "00");
    EXPECT_EQ(env.io.user_id, "00");
    EXPECT_EQ(env.io.user_name, "EmuCore");
    EXPECT_TRUE(fs::exists(save_file));

    fs::ifstream profile(temp.root / "ux0" / "user" / "00" / "user.xml");
    const std::string saved_xml((std::istreambuf_iterator<char>(profile)), std::istreambuf_iterator<char>());
    EXPECT_NE(saved_xml.find("id=\"00\" name=\"EmuCore\""), std::string::npos);
}

TEST(AppUser, customized_or_nondefault_Vita3K_profile_name_is_preserved) {
    TemporaryVitaFs temp;
    temp.write_profile("00", R"(<user id="00" name="Vita3K"><theme use-background="false"><content-id>custom-theme</content-id></theme><start-screen type="recent"><path>custom/path</path></start-screen><backgrounds><background>custom-bg</background></backgrounds></user>)");
    temp.write_profile("01", R"(<user id="01" name="Vita3K"><theme use-background="true"><content-id>default</content-id></theme><start-screen type="default"><path></path></start-screen><backgrounds/></user>)");

    EmuEnvState env;
    env.vita_fs_path = temp.root;
    env.cfg.user_id = "00";
    app::load_users(env);

    ASSERT_TRUE(app::ensure_current_user(env));
    EXPECT_EQ(env.io.user_id, "00");
    EXPECT_EQ(env.io.user_name, "Vita3K");
    EXPECT_EQ(env.app.user_list.users.at("00").name, "Vita3K");
    EXPECT_EQ(env.app.user_list.users.at("01").name, "Vita3K");
}

