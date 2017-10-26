#include <gtest/gtest.h>
#include <guild.h>
#include <fstream>
#include <iostream>

std::string read_file(std::string file_path)
{
    std::ifstream ifs{file_path};
    if (!ifs)
        return "";
    std::string line, contents;
    while (ifs) {
        std::getline(ifs, line);
        contents += line;
    }
    return contents;
}

TEST(Guild, GuildFromJson)
{
    try {
        std::string str = read_file("res/guild_create1");
        if (str.empty())
            FAIL();
        
        nlohmann::json json = nlohmann::json::parse(str);
        if (json.is_null())
            FAIL();
        cmd::discord::guild guild = json["d"];
        ASSERT_EQ("147536581748588544", guild.owner);
        ASSERT_EQ("Super Fun Time", guild.name);
        ASSERT_EQ(39, guild.members.size());
        ASSERT_EQ(16, guild.channels.size());

    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        FAIL();
    }
}

int main(int argc, char *argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
