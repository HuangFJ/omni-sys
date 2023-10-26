#include "omni.h"
#include "univalue.h"
#include <tinyformat.h>

int main(int argc, char const* argv[])
{
#ifdef FETCH_REMOTE_TX
    Init(argv[2], std::stoi(argv[3]), argv[4], argv[5]);
    std::string rawTx = argv[1];

    auto ret = ParseTx(rawTx);
    tfm::printfln("%s", ret);

    return 0;
#else
    Init();
    std::string rawTx = argv[1];
    tfm::printfln("%s ", rawTx);
    auto ret = ParseTx(RawTx::loads(rawTx));
    tfm::printfln("%s ", ret->txid);

    return 0;
#endif
}