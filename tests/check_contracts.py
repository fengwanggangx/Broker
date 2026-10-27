"""Read-only consistency check for copied XBroker/XEngine wire contracts."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
pairs = [("counter/TradeTypes.h", "trade/TradeTypes.h"), ("counter/TradeTypes.cpp", "trade/TradeTypes.cpp")]
for name in ["request.h", "request.cpp", "request.proto", "request.pb.h", "request.pb.cc", "MarketTypes.h", "MarketTypes.cpp", "v1/market.proto", "v1/market.pb.h", "v1/market.pb.cc"]:
    pairs.append(("request/" + name, "request/" + name))
for broker, xengine in pairs:
    assert (root/"XBroker/XBroker"/broker).read_bytes() == (root/"XEngine/XEngine"/xengine).read_bytes(), (broker, xengine)
print("XBroker/XEngine wire contracts match")
