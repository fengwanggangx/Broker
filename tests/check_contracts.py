"""Read-only consistency check for copied Broker/Wind wire contracts."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
pairs = [("counter/TradeTypes.h", "trading/TradeTypes.h"), ("counter/TradeTypes.cpp", "trading/TradeTypes.cpp")]
for name in ["request.h", "request.cpp", "request.proto", "request.pb.h", "request.pb.cc", "MarketTypes.h", "MarketTypes.cpp", "v1/market.proto", "v1/market.pb.h", "v1/market.pb.cc"]:
    pairs.append(("request/" + name, "request/" + name))
for broker, wind in pairs:
    assert (root/"Broker/Broker"/broker).read_bytes() == (root/"Wind/Wind"/wind).read_bytes(), (broker, wind)
print("Broker/Wind wire contracts match")
