## ADD VOICELINES here
make a directory
name it with however you want preferably the NPC's name you want to link so its easier to track

I've given a sample via john/
does not matter how you name it, the audio engine editor allows you to configure
audio trigger types (eg. impact, pickup, rollover) with whatever audio .wav you want
just note that each audio type obviously triggers differently

# for a quick guide:

- heavy.wav- Trike crashes w high velocity while NPC on mount
- mild.wav - Trike crashes w low velocity while NPC on mount
- rollover.wav - Trike rollovers while NPC on mount
- dropoff_bad.wav - If N higher than dropoff_time to get NPC to destination
- dropoff_good.wav - If N lower than dropoff_time to get NPC to destination
- hail.wav - triggers when NPC wants to ride trike (hail)
- pickup.wav - triggers once during NPC pickup
- yap.wav - NPC yapping loop at random ticks 