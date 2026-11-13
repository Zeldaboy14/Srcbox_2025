//========= Srcbox: Left 4 Dead's info_director ( basic recreation ) ==========//
//
// L4D maps start their intro themselves: info_director fires OnGameplayStart once
// the players are in, the map wires that to Trigger "relay_intro_start", and the
// relay drives the cameras, scenes and fades, calling the director's inputs
// ( StartIntro, ForceSurvivorPositions, ReleaseSurvivorPositions, FinishIntro ).
//
//=============================================================================//

#ifndef DIRECTOR_H
#define DIRECTOR_H
#ifdef _WIN32
#pragma once
#endif

// True between info_director's StartIntro and FinishIntro inputs
bool Director_IsPlayingIntro();

#endif // DIRECTOR_H
