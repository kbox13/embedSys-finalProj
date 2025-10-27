# Predicting Percussive Events to Trigger Lighting Events on an Embedded Device
### ECE M202A: Embedded Systems with Professor Mani Srivastava

This is the final project for a graduate level embedded systems course at UCLA. This project has 2 parts, first predicting events in the music, then using the predictions to trigger time synchronized events on an embedded device. 

## Percussive Event Prediction
2 avenues of percussive event detection have been explored. First, the BeatNet package that has a pre-built streaming mode to detect beats in music. The second is using Essentia, a music analysis package, to do FFT based analysis to determine percussive events. Then once events are determined, we need a predictive engine to predict future events such that scheduling and triggering can happen within the latency of communication to the embedded device. 

### Essentia Pipeline
[Essentia](https://essentia.upf.edu/) offers many prebuilt audio processing blocks that can be connected to create a DSP pipeline. The prebuilt and a few custom blocks will be combined to form the entire signal processing pipeline, going from music to future event cues that can be sent to an embedded device to trigger lighting events. 

### BeatNet
[BeatNet](https://github.com/mjhydri/BeatNet) was explored as a possible avenue to detect beats during music. After getting a working example it was determined that the function and accuracy of BeatNet was not sufficient for the project. BeatNet primarily tries to find the beat locations of a song, which for MIR tasks is helpful. However the beat often does not correspond to large percussive events in the music and as such is not helpful to synchronize lighting to.

## Triggering Events on Embedded Device

