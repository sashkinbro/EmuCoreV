#include <util/log.h>
#include <spdlog/sinks/base_sink.h>
#include <iostream>

class CaptureSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    int count = 0;
    std::string last;
protected:
    void sink_it_(const spdlog::details::log_msg &message) override {
        ++count;
        last.assign(message.payload.data(), message.payload.size());
    }
    void flush_() override {}
};

static void once(int &evaluations) {
    LOG_INFO_ONCE("once {}", ++evaluations);
}

int main() {
    auto sink = std::make_shared<CaptureSink>();
    auto logger = std::make_shared<spdlog::logger>("test", sink);
    spdlog::set_default_logger(logger);
    logger->set_level(spdlog::level::critical);
    int evaluations = 0;
    LOG_TRACE("{}", ++evaluations);
    LOG_DEBUG("{}", ++evaluations);
    LOG_INFO("{}", ++evaluations);
    LOG_WARN("{}", ++evaluations);
    LOG_ERROR("{}", ++evaluations);
    LOG_INFO_IF(++evaluations, "{}", ++evaluations);
    once(evaluations);
    if (evaluations != 0 || sink->count != 0) return 1;
    logger->set_level(spdlog::level::off);
    LOG_CRITICAL("{}", ++evaluations);
    LOG_CRITICAL_IF(++evaluations, "{}", ++evaluations);
    LOG_CRITICAL_ONCE("{}", ++evaluations);
    if (evaluations != 0 || sink->count != 0) return 4;
    logger->set_level(spdlog::level::trace);
    LOG_INFO("enabled {}", ++evaluations);
    LOG_INFO_IF(false, "{}", ++evaluations);
    once(evaluations);
    once(evaluations);
#ifdef NDEBUG
    if (evaluations != 0 || sink->count != 0) return 2;
#else
    if (evaluations != 2 || sink->count != 2 || sink->last != "once 2") return 3;
#endif
    std::cout << "Logging argument and once suppression checks passed\n";
}
