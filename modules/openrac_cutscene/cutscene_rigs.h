/* SPDX-License-Identifier: MIT */
/* OpenRAC Editor: the rigs the editor knows, by "<game>/<moby class>" ("<game>/hero" for the hero):
 * which joints are a character's jaw, eyelids and head, its moods' poses and its gaits
 * (docs/CUTSCENE_FORMAT.md, `face.rig`). A project adds its own in res://cutscene_rigs.json. */

#pragma once

namespace cutscene_rigs {

// Ratchet (rac1/hero), found in the port: joint 8 is the head (turned about -z for the game's yaw),
// 10 the jaw (opened about -x), 25 and 29 the inner brows, 26 and 30 the outer ones, 37 and 51 the
// mouth's corners. He has no eyelid joints (the game blinks him another way), so no "lids". Gaits:
// sequence 0 stands, 4 runs (the decomp's set_hero_animation). Parts (first and last joint, for a
// layer's mask): 1-7 the spine, 8-51 the head, 52-71 and 72-91 the arms, 92-103 the legs, 104-110
// the tail.
inline constexpr const char *BUILT_IN = R"RIGS({
 "rac1/hero": {
  "parts": {
   "spine": [1, 7],
   "head": [8, 51],
   "left_arm": [52, 71],
   "right_arm": [72, 91],
   "legs": [92, 103],
   "tail": [104, 110]
  },
  "jaw": {
   "joint": 10,
   "axis": [
    -1,
    0,
    0
   ],
   "open": 25
  },
  "head": {
   "joint": 8,
   "axis": [
    0,
    0,
    -1
   ],
   "limit": 60
  },
  "moods": {
   "happy": [
    {
     "joint": 37,
     "rotation": [
      0.0,
      0.0,
      0.1736,
      0.9848
     ]
    },
    {
     "joint": 51,
     "rotation": [
      -0.0,
      -0.0,
      -0.1736,
      0.9848
     ]
    },
    {
     "joint": 25,
     "rotation": [
      0.0698,
      0.0,
      0.0,
      0.9976
     ]
    },
    {
     "joint": 29,
     "rotation": [
      -0.0698,
      -0.0,
      -0.0,
      0.9976
     ]
    }
   ],
   "angry": [
    {
     "joint": 25,
     "rotation": [
      -0.1736,
      -0.0,
      -0.0,
      0.9848
     ]
    },
    {
     "joint": 29,
     "rotation": [
      0.1736,
      0.0,
      0.0,
      0.9848
     ]
    }
   ],
   "surprised": [
    {
     "joint": 25,
     "rotation": [
      0.1736,
      0.0,
      0.0,
      0.9848
     ]
    },
    {
     "joint": 29,
     "rotation": [
      -0.1736,
      -0.0,
      -0.0,
      0.9848
     ]
    },
    {
     "joint": 10,
     "rotation": [
      -0.0698,
      0.0,
      0.0,
      0.9976
     ]
    }
   ],
   "sad": [
    {
     "joint": 25,
     "rotation": [
      0.1305,
      0.0,
      0.0,
      0.9914
     ]
    },
    {
     "joint": 29,
     "rotation": [
      -0.1305,
      -0.0,
      -0.0,
      0.9914
     ]
    },
    {
     "joint": 26,
     "rotation": [
      -0.1305,
      -0.0,
      -0.0,
      0.9914
     ]
    },
    {
     "joint": 30,
     "rotation": [
      0.1305,
      0.0,
      0.0,
      0.9914
     ]
    }
   ]
  },
  "gaits": [
   {
    "seq": 0,
    "speed": 0
   },
   {
    "seq": 4,
    "speed": 1
   }
  ]
 }
})RIGS";

} // namespace cutscene_rigs
