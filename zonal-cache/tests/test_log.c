#include "test.h"
#include "../src/log.h"

TEST(level_names)
{
    CHECK_EQ_INT(log_level_from_string("debug"), ZC_LOG_DEBUG);
    CHECK_EQ_INT(log_level_from_string("INFO"), ZC_LOG_INFO);
    CHECK_EQ_INT(log_level_from_string("warn"), ZC_LOG_WARN);
    CHECK_EQ_INT(log_level_from_string("Warning"), ZC_LOG_WARN);
    CHECK_EQ_INT(log_level_from_string("error"), ZC_LOG_ERROR);
}

TEST(unknown_level_defaults_to_info)
{
    CHECK_EQ_INT(log_level_from_string("verbose"), ZC_LOG_INFO);
    CHECK_EQ_INT(log_level_from_string(""), ZC_LOG_INFO);
    CHECK_EQ_INT(log_level_from_string(NULL), ZC_LOG_INFO);
}

TEST(levels_are_ordered)
{
    CHECK(ZC_LOG_DEBUG < ZC_LOG_INFO);
    CHECK(ZC_LOG_INFO < ZC_LOG_WARN);
    CHECK(ZC_LOG_WARN < ZC_LOG_ERROR);
}

void suite_log(void)
{
    RUN(level_names);
    RUN(unknown_level_defaults_to_info);
    RUN(levels_are_ordered);
}
