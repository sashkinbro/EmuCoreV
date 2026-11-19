#include <emucorev/paf/page_turn.h>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    using namespace emucorev::paf;
    try {
        if(argc!=2) throw std::runtime_error("Usage: paf_page_material_test ORIGINAL_CORPUS");
        std::ifstream file(argv[1],std::ios::binary);
        std::array<char,8> magic{}; file.read(magic.data(),magic.size());
        if(magic!=std::array<char,8>{'P','A','F','M','A','T','0','1'}) throw std::runtime_error("Invalid material corpus");
        auto word=[&]() {
            std::uint32_t value; file.read(reinterpret_cast<char*>(&value),sizeof(value));
            if(!file) throw std::runtime_error("Truncated material corpus");
            return value;
        };
        const auto count=word();
        if(!count || count>10000) throw std::runtime_error("Invalid material case count");
        for(unsigned i=0;i<count;++i) {
            std::array<float,8> v{};
            for(auto& value:v) value=std::bit_cast<float>(word());
            const auto aspect=page_turn_normalized_aspect({v[0],v[1]});
            const auto shape=page_turn_fold_shape({v[2],v[3]},aspect);
            const auto p=page_turn_shape_material({v[2],v[3]},shape,{v[4],v[5],v[6],v[7]});
            const std::array<float,21> actual{p.deepest[0],p.deepest[1],p.deepest[2],p.deepest[3],
                p.grep.x,p.grep.y,p.params[0],p.params[1],p.params[2],p.params[3],
                p.fold_plane[0],p.fold_plane[1],p.fold_plane[2],p.fold_plane[3],
                p.aspect[0],p.aspect[1],p.aspect[2],p.aspect[3],p.normal_scale_z,
                std::bit_cast<float>(0x3f4d30adu)*p.fresnel_multiplier,
                std::bit_cast<float>(0x3e9349e0u)*p.fresnel_multiplier};
            for(unsigned component=0;component<actual.size();++component) {
                const auto expected=word();
                if(expected!=std::bit_cast<std::uint32_t>(actual[component])) {
                    std::cerr << "case=" << i << " component=" << component << std::hex
                        << " expected=" << expected << " actual=" << std::bit_cast<std::uint32_t>(actual[component]) << '\n';
                    throw std::runtime_error("Original material packing differs");
                }
            }
        }
        if(file.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Trailing material corpus data");
        std::cout << "PASS " << count << " bit-exact original material records / " << count*21 << " words\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
