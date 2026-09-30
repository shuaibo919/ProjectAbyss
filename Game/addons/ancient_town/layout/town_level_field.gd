@tool
class_name TownLevelField
extends RefCounted

## Building level from position — Qin et al. 2023 Eq 7: p_max = f_lv(f_p(x,z) + N_L + N_H⁹/10).
##
## f_p falls off with distance from the town's centre of rank (Eq 9 smooth, Eq 10 stepped), N_L
## is low-frequency noise so the odd humble house stands near the centre and the odd grand one
## far out, N_H high-frequency noise whose ninth power leaves only isolated spikes — Qin's device
## for scattering pavilions and pagodas. Several foci are allowed (a city has its 衙署, a
## waterfront its bridgehead market); the strongest wins.
##
## Two readings, noted because the paper is terse:
##   - Eq 10 rotates (x, z) by π/4 before taking a norm. Under the L2 norm a rotation changes
##     nothing, and Fig 13b shows square terraces, so the norm is taken as L∞ (a diamond).
##   - N_H⁹/10 with N_H ∈ [−1, 1] never exceeds 0.1, yet Fig 13d peaks near ±20; the spike term is
##     therefore scaled by `spike` (levels added at N_H = ±1) rather than a fixed 1/10.

enum Mode { SMOOTH, STEP }

var mode := Mode.SMOOTH
var extent := 300.0                  ## max(city.length, city.width)
var foci: Array[Vector2] = []
var low := FastNoiseLite.new()
var high := FastNoiseLite.new()
var low_amplitude := 0.8
var spike := 3.0


func _init(seed: int = 0) -> void:
	low.seed = seed
	low.frequency = 0.012
	high.seed = seed + 101
	high.frequency = 0.09


## Continuous level at `p` (layout frame), before rounding.
func value(p: Vector2) -> float:
	var fp := 1.0
	for c in foci:
		fp = maxf(fp, _falloff(p - c))
	var nh := high.get_noise_2dv(p)
	return fp + low.get_noise_2dv(p) * low_amplitude + spike * signf(nh) * pow(absf(nh), 9.0)


## Level rounded into [lo, hi] — the caller's legal range for the use (民居 2..4, 官式 5..6).
func level(p: Vector2, lo: int, hi: int) -> int:
	return clampi(roundi(value(p)), lo, hi)


func _falloff(d: Vector2) -> float:
	if mode == Mode.STEP:
		var r := d.rotated(PI * 0.25)
		var norm := maxf(maxf(absf(r.x), absf(r.y)), 1e-3)
		return ceilf(clampf(extent / (2.5 * norm), 1.0, 8.0))
	return clampf(extent / maxf(d.length(), 1e-3), 1.0, 8.0)
