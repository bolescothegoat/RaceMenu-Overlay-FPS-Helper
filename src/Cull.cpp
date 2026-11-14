#include "Cull.h"

#include <Windows.h>
#include <cctype>
#include <filesystem>
#include <toml++/toml.hpp>
#include <unordered_map>

// OverlayFpsHelper: RaceMenu (skee) creates one drawable shape per overlay slot ("Body [Ovl0..N]", "Hands", "Feet",
// "Face", plus spell overlays "[SOvl#]") on every actor, whether or not anything is painted in the slot. An empty
// slot holds skee's blank default texture, but it is still submitted to the renderer: one draw call per slot, per
// pass. Measured on one install: ~86 overlay shapes per female NPC against ~28 real parts.
//
// This plugin hides (AppCulled) every overlay shape that is empty (blank default texture), and shows it again the
// moment a real texture is written to it. Optionally (cullTransparent, off by default) it also hides alpha-0 overlays. Slot counts and every mod that paints
// overlays keep working unchanged.
//
// Cost control: each actor's overlay shapes are found once (full scene-graph walk) and cached; the regular scan only
// re-checks those cached shapes (a texture-path tail compare and an alpha read, no allocations). A full re-walk of an
// actor happens only when its 3D object changes, or every rescanMs (staggered per actor so there is no spike).
namespace Cull
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Config
		{
			bool  enabled{ true };
			int   intervalMs{ 100 };       // how often cached shapes are re-checked
			int   rescanMs{ 1000 };        // how often an actor's scene graph is re-walked to find new overlay shapes
			float radius{ 0.0f };          // 0 = every loaded actor, otherwise only actors within this many units
			bool  includePlayer{ true };
			bool  cullTransparent{ false }; // also hide overlays whose alpha is 0. OFF by default: it made a mod's alpha never land (body hair vanished)
		};

		Config            g_cfg;
		std::atomic<bool> g_pending{ false };

		struct OverlayShape
		{
			RE::NiPointer<RE::BSGeometry>  geom;
			RE::NiPointer<RE::BSShaderProperty> effect;  // owned so its address cannot be reused while we compare against it
			RE::BSLightingShaderProperty*  lsp{ nullptr };
		};

		struct ActorCache
		{
			RE::NiAVObject*           root{ nullptr };  // identity only, never dereferenced
			std::vector<OverlayShape> shapes;
			Clock::time_point         nextRescan{};
			std::uint64_t             seen{ 0 };
		};

		std::unordered_map<RE::FormID, ActorCache> g_cache;  // main thread only
		std::uint64_t                              g_tick{ 0 };

		struct Stats
		{
			std::uint64_t culledNow{ 0 };
			std::uint64_t shownNow{ 0 };
			std::uint64_t scans{ 0 };
			std::uint64_t rescans{ 0 };
			int           lastHiddenTotal{ 0 };
			int           lastActors{ 0 };
			int           lastShapes{ 0 };
			double        scanMsTotal{ 0.0 };
			double        scanMsMax{ 0.0 };
			double        rescanMsTotal{ 0.0 };
		} g_stats;

		bool EqI(char a, char b)
		{
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		}

		bool EndsWithI(const char* s, std::size_t n, const char* suffix)
		{
			const std::size_t m = std::strlen(suffix);
			if (n < m) {
				return false;
			}
			for (std::size_t i = 0; i < m; ++i) {
				if (!EqI(s[n - m + i], suffix[i])) {
					return false;
				}
			}
			return true;
		}

		// "Body [Ovl12]", "Hands [SOvl0]" ... (any case), no allocation
		bool IsOverlayName(const char* n)
		{
			if (!n) {
				return false;
			}
			const std::size_t len = std::strlen(n);
			if (len < 7 || n[len - 1] != ']') {
				return false;
			}
			const char* p = n + len - 1;
			while (p > n && *p != '[') {
				--p;
			}
			if (*p != '[' || p == n) {
				return false;
			}
			++p;
			if (EqI(p[0], 's')) {
				++p;
			}
			return EqI(p[0], 'o') && EqI(p[1], 'v') && EqI(p[2], 'l');
		}

		// skee's blank overlay texture (and an empty path) both mean "nothing painted in this slot"
		bool IsBlankTexture(const char* tex)
		{
			if (!tex || !*tex) {
				return true;
			}
			const std::size_t n = std::strlen(tex);
			return EndsWithI(tex, n, "overlays\\default.dds") || EndsWithI(tex, n, "overlays/default.dds");
		}

		void Rescan(ActorCache& c, RE::NiAVObject* root)
		{
			const auto t0 = Clock::now();
			c.shapes.clear();
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* g) {
				if (IsOverlayName(g->name.c_str())) {
					OverlayShape s;
					s.geom.reset(g);
					c.shapes.push_back(std::move(s));
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			++g_stats.rescans;
			g_stats.rescanMsTotal += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
		}

		void UpdateShapes(ActorCache& c, int& hidden, int& shapes)
		{
			for (auto& s : c.shapes) {
				RE::BSGeometry* g = s.geom.get();
				if (!g) {
					continue;
				}
				++shapes;
				auto* eff = g->GetGeometryRuntimeData().shaderProperty.get();
				if (eff != s.effect.get()) {
					s.effect.reset(eff);
					s.lsp = eff ? netimmerse_cast<RE::BSLightingShaderProperty*>(eff) : nullptr;
				}
				if (!s.lsp || !s.lsp->material) {
					continue;  // no shader yet: leave it alone
				}
				auto* ts = static_cast<RE::BSLightingShaderMaterialBase*>(s.lsp->material)->textureSet.get();
				if (!ts) {
					continue;
				}
				const char* tex = ts->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
				const bool  empty = IsBlankTexture(tex) || (g_cfg.cullTransparent && s.lsp->alpha <= 0.001f);
				if (empty) {
					if (!g->GetAppCulled()) {
						g->SetAppCulled(true);
						++g_stats.culledNow;
					}
					++hidden;
				} else if (g->GetAppCulled()) {
					g->SetAppCulled(false);  // a real texture / alpha appeared in a slot we had hidden
					++g_stats.shownNow;
				}
			}
		}

		void ScanActor(RE::Actor* a, Clock::time_point now, int& hidden, int& shapes)
		{
			auto* root = a->Get3D();
			if (!root) {
				return;
			}
			const RE::FormID key = a->GetFormID();
			auto&            c = g_cache[key];
			if (c.root != root || now >= c.nextRescan) {
				Rescan(c, root);
				c.root = root;
				const int jitter = static_cast<int>(key % static_cast<RE::FormID>(std::max(1, g_cfg.rescanMs / 2)));
				c.nextRescan = now + std::chrono::milliseconds(g_cfg.rescanMs + jitter);
			}
			c.seen = g_tick;
			UpdateShapes(c, hidden, shapes);
		}

		void Scan()
		{
			const auto t0 = Clock::now();
			auto*      player = RE::PlayerCharacter::GetSingleton();
			auto*      pl = RE::ProcessLists::GetSingleton();
			if (!player || !pl || !player->Get3D()) {
				return;
			}
			++g_tick;
			const auto ppos = player->GetPosition();
			int        hidden = 0, actors = 0, shapes = 0;
			bool       sawPlayer = false;
			for (auto& h : pl->highActorHandles) {
				auto a = h.get();
				if (!a) {
					continue;
				}
				if (a.get() == player) {
					sawPlayer = true;
					if (!g_cfg.includePlayer) {
						continue;
					}
				} else if (g_cfg.radius > 0.0f && a->GetPosition().GetDistance(ppos) > g_cfg.radius) {
					continue;
				}
				ScanActor(a.get(), t0, hidden, shapes);
				++actors;
			}
			if (!sawPlayer && g_cfg.includePlayer) {  // the player is normally not in the high-actor list
				ScanActor(player, t0, hidden, shapes);
				++actors;
			}
			for (auto it = g_cache.begin(); it != g_cache.end();) {  // forget actors that are no longer loaded
				it = (it->second.seen != g_tick) ? g_cache.erase(it) : std::next(it);
			}
			++g_stats.scans;
			g_stats.lastHiddenTotal = hidden;
			g_stats.lastActors = actors;
			g_stats.lastShapes = shapes;
			const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
			g_stats.scanMsTotal += ms;
			g_stats.scanMsMax = std::max(g_stats.scanMsMax, ms);
		}

		std::filesystem::path GameRoot()
		{
			wchar_t buf[MAX_PATH]{};
			GetModuleFileNameW(nullptr, buf, MAX_PATH);
			return std::filesystem::path(buf).parent_path();
		}

		void LoadConfig()
		{
			const auto path = GameRoot() / "Data" / "SKSE" / "Plugins" / "OverlayFpsHelper.toml";
			try {
				const auto tbl = toml::parse_file(path.string());
				g_cfg.enabled = tbl["enabled"].value_or(true);
				g_cfg.intervalMs = std::clamp(tbl["intervalMs"].value_or(100), 20, 2000);
				g_cfg.rescanMs = std::clamp(tbl["rescanMs"].value_or(1000), 100, 10000);
				g_cfg.radius = static_cast<float>(tbl["radius"].value_or(0.0));
				g_cfg.includePlayer = tbl["includePlayer"].value_or(true);
				g_cfg.cullTransparent = tbl["cullTransparent"].value_or(false);
				logger::info("config: enabled={} intervalMs={} rescanMs={} radius={} includePlayer={} cullTransparent={}  ({})",
					g_cfg.enabled, g_cfg.intervalMs, g_cfg.rescanMs, g_cfg.radius, g_cfg.includePlayer, g_cfg.cullTransparent,
					path.string());
			} catch (const std::exception& e) {
				logger::warn("config not read ({}), using defaults: {}", path.string(), e.what());
			}
		}

		void TimerThread()
		{
			auto lastLog = Clock::now();
			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(g_cfg.intervalMs));
				if (!g_cfg.enabled) {
					continue;
				}
				if (!g_pending.exchange(true)) {  // never queue a second scan while one is waiting for the main thread
					if (const auto t = SKSE::GetTaskInterface()) {
						t->AddTask([]() {
							Scan();
							g_pending = false;
						});
					} else {
						g_pending = false;
					}
				}
				const auto now = Clock::now();
				if (now - lastLog > 30s) {
					lastLog = now;
					logger::info(
						"hiding {} of {} overlay shapes on {} actors | shown again {} / hidden {} (totals) over {} scans | scan avg {:.3f} ms max {:.3f} ms | {} full re-walks avg {:.3f} ms",
						g_stats.lastHiddenTotal, g_stats.lastShapes, g_stats.lastActors, g_stats.shownNow, g_stats.culledNow,
						g_stats.scans, g_stats.scans ? g_stats.scanMsTotal / static_cast<double>(g_stats.scans) : 0.0,
						g_stats.scanMsMax, g_stats.rescans,
						g_stats.rescans ? g_stats.rescanMsTotal / static_cast<double>(g_stats.rescans) : 0.0);
				}
			}
		}
	}

	void Reset()
	{
		g_cache.clear();
		logger::info("cache cleared (load / new game)");
	}

	void Init()
	{
		LoadConfig();
		std::thread(TimerThread).detach();
		logger::info("scan timer started");
	}
}
