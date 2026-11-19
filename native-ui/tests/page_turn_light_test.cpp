#include <emucorev/paf/page_turn.h>
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    using namespace emucorev::paf;
    try {
        if(argc!=2) throw std::runtime_error("Usage: paf_page_light_test ORIGINAL_CORPUS");
        std::ifstream file(argv[1],std::ios::binary);
        std::array<char,8> magic{}; file.read(magic.data(),magic.size());
        if(magic!=std::array<char,8>{'P','A','F','L','I','T','0','1'}) throw std::runtime_error("Invalid light corpus");
        auto word=[&]() {
            std::uint32_t value; file.read(reinterpret_cast<char*>(&value),sizeof(value));
            if(!file) throw std::runtime_error("Truncated light corpus");
            return value;
        };
        const auto count=word();
        if(!count || count>10000) throw std::runtime_error("Invalid light case count");
        for(unsigned i=0;i<count;++i) {
            std::array<float,13> v{};
            for(auto& value:v) value=std::bit_cast<float>(word());
            PageTurnPointLight light;
            light.angles={v[0],v[1]}; light.distance=v[2];
            std::copy_n(v.begin()+3,3,light.attenuation.begin());
            std::copy_n(v.begin()+6,3,light.ambient.begin());
            std::copy_n(v.begin()+9,3,light.diffuse.begin());
            light.fade=v[12];
            const auto result=page_turn_light_parameters(light);
            for(unsigned component=0;component<32;++component) {
                const auto expected=word();
                const float actual=component<16?result.position[component]:result.color[component-16];
                if(expected!=std::bit_cast<std::uint32_t>(actual)) {
                    std::cerr << "case=" << i << " component=" << component << std::hex
                        << " expected=" << expected << " actual=" << std::bit_cast<std::uint32_t>(actual) << '\n';
                    throw std::runtime_error("Original page light differs");
                }
            }
        }
        if(file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Trailing light corpus data");
        std::cout << "PASS " << count << " bit-exact original page-light records\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
