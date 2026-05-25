[33md793c9a[m[33m ([m[1;36mHEAD[m[33m -> [m[1;32mmain[m[33m, [m[1;31morigin/main[m[33m)[m HEAD@{0}: reset: moving to origin/main
[33md793c9a[m[33m ([m[1;36mHEAD[m[33m -> [m[1;32mmain[m[33m, [m[1;31morigin/main[m[33m)[m HEAD@{1}: commit: feat(editor & world):
[33m62f5a46[m HEAD@{2}: commit: feat: terrain surface paint system
[33mb167f20[m HEAD@{3}: reset: moving to origin/main
[33mb167f20[m HEAD@{4}: commit: feat(editor): improved road moad, added terrain wireframes for much more convenient placement accuracy
[33m2429a2f[m HEAD@{5}: pull --rebase origin main (finish): returning to refs/heads/main
[33m2429a2f[m HEAD@{6}: pull --rebase origin main (pick): feat(editor): connect assets/ textures to road spline mode
[33m6bfa58a[m HEAD@{7}: pull --rebase origin main (start): checkout 6bfa58a2b17326b125874122bf02c56703a7fd7e
[33mc7dc2fe[m HEAD@{8}: commit: feat(editor): connect assets/ textures to road spline mode
[33mb95068d[m HEAD@{9}: commit: feat(editor) : road/terrain mode hud + road cursor
[33mf326d3a[m HEAD@{10}: commit: feat(terrain): undo stack, flatten, and editor controls reference
[33m3772016[m HEAD@{11}: commit: feat (world):
[33m07e5cc8[m HEAD@{12}: commit: feat(world): road spline & height field foundations
[33m0a61688[m HEAD@{13}: commit: feat: F5 hot-refresh prop list in editor
[33mfb40090[m HEAD@{14}: commit: feat: alpha cutout for leaf/foliage textures
[33me8c7544[m HEAD@{15}: commit: feat(editor): added precision translate values + precision select for
[33m759a365[m HEAD@{16}: commit: fix(physics): correct dynamic object rotation double-apply on drive mode init
[33m8ec63e2[m HEAD@{17}: reset: moving to origin/main
[33m8ec63e2[m HEAD@{18}: commit: fix : rotation bug duped causes wrong pos in editor vs drive mode
[33mb5e444a[m HEAD@{19}: commit: feat : R toggle resets trike and dyn state to original pos for easier testing
[33me539089[m HEAD@{20}: commit: feat(editor): add vertical stacking for future multiple stacking dyn props + modified LMB + Shift to toggle vert select
[33m574b069[m HEAD@{21}: commit: feat(editor): add Ctrl+C / Ctrl+V copy-paste for placed objects
[33m295ead8[m HEAD@{22}: commit: feat(renderer): add UV + texture support to OBJ pipeline
[33me6a5cb6[m HEAD@{23}: commit: fix: prop wireframe AABB now accounts for rotation
[33ma0b0e41[m HEAD@{24}: reset: moving to HEAD
[33ma0b0e41[m HEAD@{25}: commit: fix(physics): added gravity assisted tipping so once obj is past 90 deg we kill angular and snap flat = obj fall properly
[33mceb9ba3[m HEAD@{26}: commit: feat (physics): rigid body, tipping, dynamic x dynamic collision. Cones collision are immaculate. LETS FUCKING GOOOOO
[33m646c025[m HEAD@{27}: commit: fix(collision): kill lateral buildup (for real this time), full clamp post-collision speed to pre-impact
[33m0155f2c[m HEAD@{28}: commit: fix(physics): fix: reverse gear, collision bleed, restitution overshoot, chase cam
[33m8ccf1bc[m HEAD@{29}: commit: fix(collision): resolve infinite impact loop and lateral bleed on world prop AABBs
[33m54b9535[m HEAD@{30}: commit: feat(renderer, world, core): fix map save issue
[33m3bb1090[m HEAD@{31}: commit: fix(editor):
[33m5ca97f4[m HEAD@{32}: commit: feat(editor): behavior cycle key + real mesh AABB for wireframes and picking
[33m3fd7d24[m HEAD@{33}: commit: feat (editor): auto floor props + y nudge on translate editor tool
[33m79c5aeb[m HEAD@{34}: commit: feat(editor): render placed obj props with lit shader
[33me26bf3a[m HEAD@{35}: commit: feat (editor):
[33m8a71ee1[m HEAD@{36}: commit: feat (editor):
[33m74e463e[m HEAD@{37}: commit: feat (editor) :
[33m9777694[m HEAD@{38}: commit: feat(editor):
[33m7374b99[m HEAD@{39}: commit: feat (editor) : added smart LMB select vs place with AABB raycast
[33m627c3eb[m HEAD@{40}: commit: feat (editor):
[33m5a44adf[m HEAD@{41}: commit: feat(editor):
[33m0ca52ca[m HEAD@{42}: commit: feat: world/:
[33m3cdca54[m HEAD@{43}: pull --rebase origin main (finish): returning to refs/heads/main
[33m3cdca54[m HEAD@{44}: pull --rebase origin main (pick): feat: world object defs:
[33m27d028d[m HEAD@{45}: pull --rebase origin main (start): checkout 27d028d4e77c33a71a5dde3f9c3e02c12c375433
[33m4a04042[m HEAD@{46}: commit: feat: world object defs:
[33md666897[m HEAD@{47}: commit: fix (tuning physics):
[33me9effc7[m HEAD@{48}: commit: finally fixed diagonal directional issue:
[33m1c0ef29[m HEAD@{49}: commit: proc mesh toggle
[33m106ae75[m HEAD@{50}: commit: scene refactor:
[33mccda3df[m HEAD@{51}: commit: cam shake on impact
[33mbab7638[m HEAD@{52}: commit: implemented solid obstacle mesh & red flash on impact
[33m9f65971[m HEAD@{53}: commit: impact response implementation:
[33md158b46[m HEAD@{54}: commit: collision stop. No energy transfer & impact response yet. Newton is tweaking
[33m1e41f79[m HEAD@{55}: commit: obstacle wireframe
[33m79e02d4[m HEAD@{56}: commit: AABB hitbox init wireframe
[33mbe9e4cc[m HEAD@{57}: commit: small physics improvs, const tweaks, mesh debug
[33mf784493[m HEAD@{58}: commit: initial goofy rollover physics no realistic crash & collision yet
[33m9d80e3b[m HEAD@{59}: commit: lateral dynamics & rollover defs
[33mfb570c9[m HEAD@{60}: commit: cam lerp. LOTS of tweaks and tests, Smashing the keyboard
[33mbfe7142[m HEAD@{61}: commit: custom hud & physics tuning pass 1
[33m0d48b91[m HEAD@{62}: commit: ground check & rgb axis markers. debugging trike default direction issue
[33m276a4ae[m HEAD@{63}: commit: initial chase cam
[33meb517a6[m HEAD@{64}: commit: initial trike physics state
[33m946329c[m HEAD@{65}: commit: hardcoded slop cleanup in core
[33m7213f22[m HEAD@{66}: commit: hardcoded slop cleanup wip. More const.hpp defs
[33m6da636d[m HEAD@{67}: pull --rebase origin main (finish): returning to refs/heads/main
[33m6da636d[m HEAD@{68}: pull --rebase origin main (pick): mesh model render test. Ground plane added
[33m9d82f0c[m HEAD@{69}: pull --rebase origin main (start): checkout 9d82f0cdc89985610ac583a33e362566f4ea6335
[33m4edb852[m HEAD@{70}: commit: mesh model render test. Ground plane added
[33m12614a4[m HEAD@{71}: Branch: renamed refs/heads/master to refs/heads/main
[33m12614a4[m HEAD@{73}: commit (initial): initial engine, window, shader, mesh pipeline test
