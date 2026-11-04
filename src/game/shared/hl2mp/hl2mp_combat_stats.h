#ifndef HL2MP_COMBAT_STATS_H
#define HL2MP_COMBAT_STATS_H

// Server-owned, per-round counters. Bound accumulation for long-running servers.
struct HL2MPCombatStats
{
    int shots = 0, hits = 0, headHits = 0, damage = 0, kills = 0;
    static int Add(int value, int amount)
    {
        const int limit = 1000000000;
        return amount <= 0 ? value : amount >= limit - value ? limit : value + amount;
    }
    void Fired(int count) { shots = Add(shots, count); }
    void Hit(bool head) { hits = Add(hits, 1); if (head) headHits = Add(headHits, 1); }
    void Damaged(int amount, bool killed) { damage = Add(damage, amount); if (killed) kills = Add(kills, 1); }
    static float Percentage(int numerator, int denominator)
    { return denominator > 0 ? 100.0f * numerator / denominator : 0.0f; }
};
#endif
