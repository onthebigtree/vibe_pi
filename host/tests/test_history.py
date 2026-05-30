"""Tests for the SQLite usage-history store."""

import pytest

from src.history import UsageHistory


@pytest.fixture
def hist(tmp_path):
    h = UsageHistory(path=tmp_path / "history.db", flush_interval=60.0, retention_days=90)
    yield h
    h.close()


def _sample(cost, tokens=1000, status="active", pct5=10.0, pct7=5.0, c5=None, c7=None):
    return {
        "status": status,
        "tokens_used": tokens,
        "cost_usd": cost,
        "cost_5h": cost if c5 is None else c5,
        "cost_7d": cost if c7 is None else c7,
        "pct_5h": pct5,
        "pct_7d": pct7,
    }


def test_schema_created_empty(hist):
    assert hist.count() == 0
    assert hist.tools() == []


def test_add_and_flush(hist):
    staged = hist.add({"claude_code": _sample(0.5), "codex": _sample(0.2)}, ts=1000)
    assert staged == 2
    # staged but not yet written
    assert hist.count() == 0
    written = hist.flush()
    assert written == 2
    assert hist.count() == 2
    assert set(hist.tools()) == {"claude_code", "codex"}


def test_system_and_nondict_skipped(hist):
    staged = hist.add(
        {"claude_code": _sample(0.1), "system": _sample(9.9), "junk": 42, "none": None},
        ts=1000,
    )
    assert staged == 1
    hist.flush()
    assert hist.tools() == ["claude_code"]


def test_collector_field_name_aliases(hist):
    # claude_code emits usage_5h_pct / usage_7d_pct, not pct_5h / pct_7d.
    hist.add({"claude_code": {
        "status": "active", "tokens_used": 500, "cost_usd": 1.0,
        "usage_5h_pct": 88.0, "usage_7d_pct": 42.0,
    }}, ts=1000)
    hist.flush()
    row = hist.latest("claude_code")
    assert row["pct_5h"] == 88.0
    assert row["pct_7d"] == 42.0


def test_missing_fields_default_to_zero(hist):
    hist.add({"codex": {"status": "idle"}}, ts=1000)  # no numeric fields
    hist.flush()
    row = hist.latest("codex")
    assert row["cost_usd"] == 0.0
    assert row["tokens_used"] == 0
    assert row["status"] == "idle"


def test_latest_returns_most_recent(hist):
    hist.add({"claude_code": _sample(0.1)}, ts=1000)
    hist.add({"claude_code": _sample(0.3)}, ts=2000)
    hist.add({"claude_code": _sample(0.2)}, ts=1500)
    hist.flush()
    row = hist.latest("claude_code")
    assert row["ts"] == 2000
    assert row["cost_usd"] == 0.3
    assert hist.latest("nope") is None


def test_series_ordered_and_windowed(hist):
    for ts, cost in [(1000, 0.1), (2000, 0.2), (3000, 0.3)]:
        hist.add({"claude_code": _sample(cost)}, ts=ts)
    hist.flush()
    s = hist.series("claude_code", "cost_usd", since_ts=1500)
    assert s == [(2000, 0.2), (3000, 0.3)]
    s2 = hist.series("claude_code", "cost_usd", since_ts=0, until_ts=2000)
    assert s2 == [(1000, 0.1), (2000, 0.2)]


def test_series_rejects_unknown_metric(hist):
    with pytest.raises(ValueError):
        hist.series("claude_code", "drop table", since_ts=0)


def test_burn_rate_from_cumulative_delta(hist):
    # cumulative cost climbs $0.60 over 600s -> $0.06/min
    hist.add({"claude_code": _sample(1.00)}, ts=10_000)
    hist.add({"claude_code": _sample(1.60)}, ts=10_600)
    hist.flush()
    rate = hist.burn_rate("claude_code", window_s=1800, now=10_600)
    assert rate == pytest.approx(0.06, rel=1e-6)


def test_burn_rate_zero_with_one_sample(hist):
    hist.add({"claude_code": _sample(1.0)}, ts=10_000)
    hist.flush()
    assert hist.burn_rate("claude_code", now=10_000) == 0.0


def test_burn_rate_never_negative(hist):
    # cost should never drop, but if it does (e.g. window reset), clamp to 0
    hist.add({"claude_code": _sample(2.0)}, ts=10_000)
    hist.add({"claude_code": _sample(0.5)}, ts=10_600)
    hist.flush()
    assert hist.burn_rate("claude_code", now=10_600) == 0.0


def test_daily_peaks_per_utc_day(hist):
    day1 = 1_700_000_000      # some ts
    day2 = day1 + 86400
    hist.add({"claude_code": _sample(0.1, c7=0.1)}, ts=day1)
    hist.add({"claude_code": _sample(0.4, c7=0.4)}, ts=day1 + 100)  # higher same day
    hist.add({"claude_code": _sample(0.2, c7=0.2)}, ts=day2)
    hist.flush()
    daily = hist.daily("claude_code", "cost_7d", days=10, now=day2 + 10)
    assert len(daily) == 2
    assert max(daily.values()) == 0.4


def test_prune_removes_old(hist):
    now = 1_700_000_000
    hist.add({"claude_code": _sample(0.1)}, ts=now - 200 * 86400)  # old
    hist.add({"claude_code": _sample(0.2)}, ts=now - 1 * 86400)    # recent
    hist.flush()
    assert hist.count() == 2
    deleted = hist.prune(now=now)
    assert deleted == 1
    assert hist.count() == 1
    assert hist.latest("claude_code")["cost_usd"] == 0.2


async def test_record_buffers_then_flushes_on_interval(hist):
    # first record: ts far past last_flush(0) -> flushes immediately
    await hist.record({"claude_code": _sample(0.1)}, ts=100_000)
    assert hist.count() == 1
    # next record within interval -> staged, not flushed
    await hist.record({"claude_code": _sample(0.2)}, ts=100_030)
    assert hist.count() == 1
    # past the 60s interval -> flushes both pending
    await hist.record({"claude_code": _sample(0.3)}, ts=100_070)
    assert hist.count() == 3


def test_close_flushes_pending(tmp_path):
    h = UsageHistory(path=tmp_path / "h.db", flush_interval=60.0)
    h.add({"claude_code": _sample(0.5)}, ts=1000)
    assert h.count() == 0  # staged in buffer, not yet committed
    h.close()              # close() must flush the buffer first
    # reopen and confirm the row actually persisted
    h2 = UsageHistory(path=tmp_path / "h.db")
    assert h2.count() == 1
    h2.close()
