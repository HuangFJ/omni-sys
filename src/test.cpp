#include "omni.h"
#include <tinyformat.h>

int main(int argc, char const* argv[])
{
#ifdef FETCH_REMOTE_TX
    Init(argv[3], std::stoi(argv[4]), argv[5], argv[6]);
    std::string rawTx = argv[1];
    int blockHeight = std::stoi(argv[2]);

    auto ret = ParseTx(rawTx, blockHeight);
    tfm::printfln("%s", ret);
#endif
    Init();
    std::string rawTx = argv[1];
    int blockHeight = std::stoi(argv[2]);
    std::string vinsJson = argv[3];
    auto ret = ParseTx(rawTx, blockHeight, vinsJson);
    tfm::printfln("%s", ret);

    return 0;
}