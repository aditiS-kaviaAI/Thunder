#include <gtest/gtest.h>
#include <string>
#include "../../../Source/Thunder/SecurityPath.h"

TEST(SecurityPathP1, ControllerSegmentBoundary)
{
    const std::string controller = "/Service/Controller";
    EXPECT_TRUE(Thunder::PluginHost::Detail::IsControllerPath(controller, controller));
    EXPECT_TRUE(Thunder::PluginHost::Detail::IsControllerPath(
        std::string("/Service/Controller/status"), controller));
    for (const char* rejected : {"", "/Service/Control", "/Service/ControllerEvil",
             "/Service/Controller2/status", "/Other/Controller"}) {
        EXPECT_FALSE(Thunder::PluginHost::Detail::IsControllerPath(
            std::string(rejected), controller));
    }
}
