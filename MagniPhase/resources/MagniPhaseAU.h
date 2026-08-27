
#include <TargetConditionals.h>
#if TARGET_OS_IOS == 1 || TARGET_OS_VISION == 1
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif

#define IPLUG_AUVIEWCONTROLLER IPlugAUViewController_vMagniPhase
#define IPLUG_AUAUDIOUNIT IPlugAUAudioUnit_vMagniPhase
#import <MagniPhaseAU/IPlugAUViewController.h>
#import <MagniPhaseAU/IPlugAUAudioUnit.h>

//! Project version number for MagniPhaseAU.
FOUNDATION_EXPORT double MagniPhaseAUVersionNumber;

//! Project version string for MagniPhaseAU.
FOUNDATION_EXPORT const unsigned char MagniPhaseAUVersionString[];

@class IPlugAUViewController_vMagniPhase;
