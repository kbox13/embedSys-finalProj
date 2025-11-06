#!/usr/bin/env python3
"""
macOS Audio Setup Helper for BeatNet Streaming

This script helps set up audio routing on macOS to capture system audio
for BeatNet analysis. It provides instructions and checks for common
audio routing solutions.

Requirements:
- macOS system
- Audio routing software (BlackHole, SoundFlower, or similar)
"""

import subprocess
import sys
import os
import platform


def check_macos():
    """Check if running on macOS"""
    if platform.system() != "Darwin":
        print("❌ This script is designed for macOS only.")
        print("For other systems, please refer to the BeatNet documentation.")
        return False
    return True


def check_audio_permissions():
    """Check if microphone permissions are granted"""
    print("🔍 Checking audio permissions...")
    
    # Try to list audio devices to check permissions
    try:
        result = subprocess.run(['system_profiler', 'SPAudioDataType'], 
                              capture_output=True, text=True, timeout=10)
        if result.returncode == 0:
            print("✅ Audio permissions appear to be granted")
            return True
        else:
            print("⚠️  Audio permissions may not be granted")
            return False
    except Exception as e:
        print(f"⚠️  Could not check audio permissions: {e}")
        return False


def check_blackhole():
    """Check if BlackHole is installed"""
    print("🔍 Checking for BlackHole...")
    
    try:
        # Method 1: Check if BlackHole appears in audio devices
        result = subprocess.run(['system_profiler', 'SPAudioDataType'], 
                              capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0 and 'blackhole' in result.stdout.lower():
            print("✅ BlackHole is installed and available")
            return True
        
        # Method 2: Check if BlackHole driver files exist
        blackhole_paths = [
            '/Library/Audio/Plug-Ins/HAL/BlackHole.driver',
            '/System/Library/Audio/Plug-Ins/HAL/BlackHole.driver',
            '/usr/local/lib/BlackHole.driver'
        ]
        
        for path in blackhole_paths:
            if os.path.exists(path):
                print("✅ BlackHole driver found")
                return True
        
        # Method 3: Check if BlackHole appears in kextstat (for older macOS)
        try:
            result = subprocess.run(['kextstat'], capture_output=True, text=True, timeout=5)
            if result.returncode == 0 and 'blackhole' in result.stdout.lower():
                print("✅ BlackHole kernel extension found")
                return True
        except FileNotFoundError:
            # kextstat not available on newer macOS versions
            pass
        
        print("❌ BlackHole not found")
        return False
        
    except Exception as e:
        print(f"⚠️  Could not check for BlackHole: {e}")
        return False


def check_soundflower():
    """Check if SoundFlower is installed"""
    print("🔍 Checking for SoundFlower...")
    
    try:
        # Method 1: Check if SoundFlower appears in audio devices
        result = subprocess.run(['system_profiler', 'SPAudioDataType'], 
                              capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0 and 'soundflower' in result.stdout.lower():
            print("✅ SoundFlower is installed and available")
            return True
        
        # Method 2: Check if SoundFlower driver files exist
        soundflower_paths = [
            '/Library/Audio/Plug-Ins/HAL/SoundFlower.driver',
            '/System/Library/Audio/Plug-Ins/HAL/SoundFlower.driver',
            '/usr/local/lib/SoundFlower.driver'
        ]
        
        for path in soundflower_paths:
            if os.path.exists(path):
                print("✅ SoundFlower driver found")
                return True
        
        # Method 3: Check if SoundFlower appears in kextstat (for older macOS)
        try:
            result = subprocess.run(['kextstat'], capture_output=True, text=True, timeout=5)
            if result.returncode == 0 and 'soundflower' in result.stdout.lower():
                print("✅ SoundFlower kernel extension found")
                return True
        except FileNotFoundError:
            # kextstat not available on newer macOS versions
            pass
        
        print("❌ SoundFlower not found")
        return False
        
    except Exception as e:
        print(f"⚠️  Could not check for SoundFlower: {e}")
        return False


def list_audio_devices():
    """List available audio devices"""
    print("\n🎵 Available Audio Devices:")
    print("-" * 50)
    
    try:
        # Use system_profiler to get audio device info
        result = subprocess.run(['system_profiler', 'SPAudioDataType'], 
                              capture_output=True, text=True, timeout=15)
        
        if result.returncode == 0:
            lines = result.stdout.split('\n')
            current_device = None
            
            for line in lines:
                line = line.strip()
                if line.endswith(':'):
                    current_device = line[:-1]
                elif 'Default Output Device: Yes' in line and current_device:
                    print(f"🔊 {current_device}")
                elif 'Default Input Device: Yes' in line and current_device:
                    print(f"🎤 {current_device}")
                elif line and not line.startswith(' ') and ':' in line:
                    print(f"📱 {line}")
        else:
            print("❌ Could not retrieve audio device information")
            
    except Exception as e:
        print(f"❌ Error listing audio devices: {e}")


def print_setup_instructions():
    """Print setup instructions for audio routing"""
    print("\n📋 Audio Setup Instructions:")
    print("=" * 50)
    
    print("\n1. Install Audio Routing Software:")
    print("   Option A - BlackHole (Recommended):")
    print("   • Download from: https://github.com/ExistentialAudio/BlackHole")
    print("   • Install the .pkg file")
    print("   • Restart your Mac")
    
    print("\n   Option B - SoundFlower:")
    print("   • Download from: https://github.com/mattingalls/Soundflower")
    print("   • Install the .pkg file")
    print("   • Restart your Mac")
    
    print("\n2. Configure Audio Routing:")
    print("   • Open System Preferences > Sound")
    print("   • Set Output to BlackHole 2ch (or SoundFlower 2ch)")
    print("   • This routes all system audio to the virtual device")
    
    print("\n3. Configure Applications:")
    print("   • For music apps (Spotify, Apple Music, etc.):")
    print("     - Set output to BlackHole/SoundFlower")
    print("   • For system audio:")
    print("     - Use the virtual device as system output")
    
    print("\n4. Run BeatNet:")
    print("   • Use --list-devices to see available input devices")
    print("   • Use --device X to specify the virtual audio device")
    print("   • Example: python beatnet_streaming.py --device 1")


def print_troubleshooting():
    """Print troubleshooting tips"""
    print("\n🔧 Troubleshooting Tips:")
    print("=" * 30)
    
    print("\n• Audio Permission Issues:")
    print("  - Go to System Preferences > Security & Privacy > Privacy")
    print("  - Select 'Microphone' from the left sidebar")
    print("  - Add Terminal or your Python app to the list")
    print("  - Restart the application")
    
    print("\n• No Audio Input:")
    print("  - Check that the virtual audio device is set as output")
    print("  - Verify music is playing through the virtual device")
    print("  - Try increasing the volume")
    print("  - Check that the correct input device is selected")
    
    print("\n• Poor Beat Detection:")
    print("  - Ensure audio is loud enough (BeatNet works best with mastered audio)")
    print("  - Try different BeatNet models (--model 1, 2, or 3)")
    print("  - Reduce background noise")
    print("  - Use high-quality audio sources")
    
    print("\n• Performance Issues:")
    print("  - Use --thread flag for better performance")
    print("  - Close other audio applications")
    print("  - Ensure sufficient CPU resources")


def main():
    """Main function"""
    print("🎵 BeatNet macOS Audio Setup Helper")
    print("=" * 40)
    
    if not check_macos():
        sys.exit(1)
    
    print("\n🔍 Running System Checks...")
    print("-" * 30)
    
    # Check permissions
    permissions_ok = check_audio_permissions()
    
    # Check for audio routing software
    blackhole_installed = check_blackhole()
    soundflower_installed = check_soundflower()
    
    # List audio devices
    list_audio_devices()
    
    # Provide setup instructions
    if not (blackhole_installed or soundflower_installed):
        print("\n⚠️  No audio routing software detected!")
        print_setup_instructions()
    else:
        print("\n✅ Audio routing software detected!")
        print("\nNext steps:")
        print("1. Configure your music app to output to the virtual audio device")
        print("2. Run: python beatnet_streaming.py --list-devices")
        print("3. Run: python beatnet_streaming.py --device <device_id>")
    
    # Always show troubleshooting
    print_troubleshooting()
    
    print("\n🎉 Setup complete! You're ready to analyze beats!")


if __name__ == "__main__":
    main()
