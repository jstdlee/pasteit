#include "privacy/name_lists.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_set>

namespace pasteit {
namespace {

std::string lower(std::string_view word) {
    std::string out{word};
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

const std::unordered_set<std::string>& given_names() {
    static const std::unordered_set<std::string> names{
        "james", "john", "robert", "michael", "william", "david", "richard", "joseph", "thomas", "charles",
        "christopher", "daniel", "matthew", "anthony", "mark", "donald", "steven", "paul", "andrew", "joshua",
        "kenneth", "kevin", "brian", "george", "timothy", "ronald", "edward", "jason", "jeffrey", "ryan",
        "jacob", "gary", "nicholas", "eric", "jonathan", "stephen", "larry", "justin", "scott", "brandon",
        "benjamin", "samuel", "gregory", "alexander", "frank", "patrick", "raymond", "jack", "dennis", "jerry",
        "tyler", "aaron", "jose", "adam", "nathan", "henry", "douglas", "zachary", "peter", "kyle",
        "ethan", "walter", "noah", "jeremy", "christian", "keith", "roger", "terry", "austin", "sean",
        "gerald", "carl", "harold", "dylan", "arthur", "lawrence", "jordan", "jesse", "bryan", "billy",
        "bruce", "gabriel", "joe", "logan", "albert", "willie", "alan", "eugene", "russell", "vincent",
        "philip", "bobby", "johnny", "bradley", "roy", "ralph", "louis", "randy", "liam", "lucas",
        "oliver", "elijah", "mason", "leo", "owen", "max", "tom", "tim", "sam", "ben", "dan", "alex",
        "mary", "patricia", "jennifer", "linda", "elizabeth", "barbara", "susan", "jessica", "sarah", "karen",
        "lisa", "nancy", "betty", "sandra", "margaret", "ashley", "kimberly", "emily", "donna", "michelle",
        "carol", "amanda", "melissa", "deborah", "stephanie", "dorothy", "rebecca", "sharon", "laura", "cynthia",
        "amy", "kathleen", "angela", "shirley", "brenda", "emma", "anna", "pamela", "nicole", "samantha",
        "katherine", "christine", "helen", "debra", "rachel", "carolyn", "janet", "maria", "catherine", "heather",
        "diane", "olivia", "julie", "joyce", "victoria", "ruth", "virginia", "lauren", "kelly", "christina",
        "joan", "evelyn", "judith", "andrea", "hannah", "megan", "cheryl", "jacqueline", "martha", "madison",
        "teresa", "gloria", "sara", "janice", "ann", "kathryn", "abigail", "sophia", "frances", "jean",
        "alice", "judy", "isabella", "julia", "grace", "amber", "denise", "danielle", "marilyn", "beverly",
        "charlotte", "natalie", "theresa", "diana", "brittany", "doris", "kayla", "alexis", "lori", "marie",
        "ava", "mia", "chloe", "zoe", "lily", "ella", "jane", "kate", "lucy", "amelia", "harper", "ella",
        "wei", "ming", "hui", "jun", "li", "xin", "yan", "ying", "jie", "hao", "lei", "min", "ling", "yu",
        "raj", "priya", "amit", "anil", "sunil", "ravi", "arjun", "rohan", "neha", "pooja", "ahmed", "mohamed",
        "muhammad", "ali", "fatima", "omar", "hassan", "yusuf", "siti", "nur", "aisyah", "hiroshi", "yuki",
        "takeshi", "haruto", "sakura", "minjun", "seoyeon", "jiho",
    };
    return names;
}

const std::unordered_set<std::string>& surnames() {
    static const std::unordered_set<std::string> names{
        "smith", "johnson", "williams", "brown", "jones", "garcia", "miller", "davis", "rodriguez", "martinez",
        "hernandez", "lopez", "gonzalez", "wilson", "anderson", "thomas", "taylor", "moore", "jackson", "martin",
        "lee", "perez", "thompson", "white", "harris", "sanchez", "clark", "ramirez", "lewis", "robinson",
        "walker", "young", "allen", "king", "wright", "scott", "torres", "nguyen", "hill", "flores",
        "green", "adams", "nelson", "baker", "hall", "rivera", "campbell", "mitchell", "carter", "roberts",
        "gomez", "phillips", "evans", "turner", "diaz", "parker", "cruz", "edwards", "collins", "reyes",
        "stewart", "morris", "morales", "murphy", "cook", "rogers", "gutierrez", "ortiz", "morgan", "cooper",
        "peterson", "bailey", "reed", "kelly", "howard", "ramos", "kim", "cox", "ward", "richardson",
        "watson", "brooks", "chavez", "wood", "james", "bennett", "gray", "mendoza", "ruiz", "hughes",
        "price", "alvarez", "castillo", "sanders", "patel", "myers", "long", "ross", "foster", "jimenez",
        "doe", "tan", "lim", "ng", "wong", "chan", "chen", "wang", "zhang", "liu", "yang", "huang", "zhao",
        "wu", "zhou", "xu", "sun", "ma", "zhu", "hu", "guo", "he", "lin", "luo", "gao", "zheng", "liang",
        "goh", "teo", "koh", "chua", "ong", "low", "yeo", "sim", "toh", "tay", "chong", "leong", "yap",
        "singh", "kumar", "sharma", "gupta", "khan", "rahman", "tanaka", "suzuki", "sato", "watanabe",
        "park", "choi", "jung", "kang", "cho", "yoon", "jang", "schmidt", "schneider", "fischer", "weber",
        "meyer", "wagner", "becker", "dubois", "bernard", "rossi", "russo", "ferrari", "silva", "santos",
    };
    return names;
}

// The most common Chinese family names (single characters, UTF-8).
constexpr std::array<std::string_view, 100> kChineseSurnames{
    "王", "李", "张", "刘", "陈", "杨", "黄", "赵", "吴", "周", "徐", "孙", "马", "朱", "胡", "郭", "何", "林", "罗", "高",
    "郑", "梁", "谢", "宋", "唐", "许", "韩", "冯", "邓", "曹", "彭", "曾", "肖", "田", "董", "袁", "潘", "于", "蒋", "蔡",
    "余", "杜", "叶", "程", "苏", "魏", "吕", "丁", "任", "沈", "姚", "卢", "姜", "崔", "钟", "谭", "陆", "汪", "范", "金",
    "石", "廖", "贾", "夏", "韦", "付", "方", "白", "邹", "孟", "熊", "秦", "邱", "江", "尹", "薛", "闫", "段", "雷", "侯",
    "龙", "史", "陶", "黎", "贺", "顾", "毛", "郝", "龚", "邵", "万", "钱", "严", "覃", "武", "戴", "莫", "孔", "向", "汤",
};

}  // namespace

bool is_common_given_name(std::string_view word) {
    return given_names().contains(lower(word));
}

bool is_common_surname(std::string_view word) {
    return surnames().contains(lower(word));
}

bool is_chinese_surname(std::string_view character) {
    return std::find(kChineseSurnames.begin(), kChineseSurnames.end(), character) != kChineseSurnames.end();
}

}  // namespace pasteit
