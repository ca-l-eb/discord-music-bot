#include <gtest/gtest.h>
#include <discord.h>
#include <gateway_store.h>
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
        std::string str = read_file("./res/guild_create1");
        if (str.empty())
            FAIL();
        
        nlohmann::json json = nlohmann::json::parse(str);
        if (json.is_null())
            FAIL();
        discord::guild guild = json["d"];
        ASSERT_EQ(147536581748588544, guild.owner);
        ASSERT_EQ("Super Fun Time", guild.name);
        ASSERT_EQ(39, guild.members.size());
        ASSERT_EQ(16, guild.channels.size());

    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        FAIL();
    }
}

TEST(GatewayStore, LoadStore)
{
     try {
        std::string g1 = read_file("./res/guild_create1");
        std::string g2 = read_file("./res/guild_create2");
        if (g1.empty() || g2.empty())
            FAIL();
        
        nlohmann::json json1 = nlohmann::json::parse(g1);
        nlohmann::json json2 = nlohmann::json::parse(g2);
        
        if (json1.is_null() || json2.is_null())
            FAIL();
        
        discord::gateway_store store;
        store.guild_create(json1["d"]);
        store.guild_create(json2["d"]);
        
        auto s = store.lookup_channel(1);
        EXPECT_EQ(0, s);
        
        // Lookup a channel id in the store structure, in this case they are the same id
        auto guild_id = store.lookup_channel(312472384026181632);
        EXPECT_EQ(312472384026181632, guild_id);
        
        // Same thing but with a different channel, this time with a different id
        guild_id = store.lookup_channel(312472384026181633);
        EXPECT_EQ(312472384026181632, guild_id);
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
