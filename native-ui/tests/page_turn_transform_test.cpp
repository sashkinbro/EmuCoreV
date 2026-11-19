#include <emucorev/paf/page_turn.h>
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    using namespace emucorev::paf;
    try {
        if(argc!=2) throw std::runtime_error("Usage: paf_page_transform_test ORIGINAL_CORPUS");
        std::ifstream file(argv[1],std::ios::binary);
        std::array<char,8> magic{}; file.read(magic.data(),magic.size());
        if(magic!=std::array<char,8>{'P','A','F','P','O','S','0','1'}) throw std::runtime_error("Invalid transform corpus");
        auto word=[&]() {
            std::uint32_t value; file.read(reinterpret_cast<char*>(&value),sizeof(value));
            if(!file) throw std::runtime_error("Truncated transform corpus");
            return value;
        };
        const auto count=word();
        if(!count || count>10000) throw std::runtime_error("Invalid transform case count");
        for(unsigned i=0;i<count;++i) {
            std::array<float,44> v{};
            for(auto& value:v) value=std::bit_cast<float>(word());
            PageTurnPose pose;
            std::copy_n(v.begin()+2,3,pose.scale.begin());
            std::copy_n(v.begin()+5,3,pose.position.begin());
            std::copy_n(v.begin()+8,4,pose.rotation.begin());
            PageTurnMatrix projection,view;
            std::copy_n(v.begin()+12,16,projection.begin());
            std::copy_n(v.begin()+28,16,view.begin());
            const auto result=page_turn_transforms({v[0],v[1]},pose,projection,view);
            for(unsigned component=0;component<32;++component) {
                const auto expected=word();
                const float actual=component<16?result.model[component]:result.model_view_projection[component-16];
                if(expected!=std::bit_cast<std::uint32_t>(actual)) {
                    std::cerr << "case=" << i << " component=" << component << std::hex
                        << " expected=" << expected << " actual=" << std::bit_cast<std::uint32_t>(actual) << '\n';
                    throw std::runtime_error("Original page transform differs");
                }
            }
        }
        if(file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Trailing transform corpus data");
        std::cout << "PASS " << count << " bit-exact original page-transform records\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
