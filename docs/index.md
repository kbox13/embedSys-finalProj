---
layout: page
title: Project Overview
permalink: /
exclude: true
---
<!-- Website must have initial sections for goals, specific aims, approach, deliverables, and timeline-->
# Predicting Percussive Events to Trigger Lighting Events on an Embedded Device

## Project Abstract
This project is meant to synchronize lighting to music to create interactive visuals. The project has 2 parts, first determining events in the music, then using the events to trigger time synchronized events on an embedded device. To do this events will need to be predicted such that the latency between the platform doing the processing and the embedded device can be accounted for.

## Project Goals
The goals of the project can be divided into 2 parts, the signal processing side and the scheduling and embedded device action side  

### Percussive Event Prediction
- Detect Percussive Events in Music 
- Use a prediction algorithm to predict future events 100ms into the future.
- Visualize events and predicted events on host laptop
- Measure accuracy of predicted events to actual detected events.
- Make time synchronized predicted events availible for communication to embedded device.

### Embedded Device Triggering
- Synchronize and Measure time/clock difference between host laptop and embedded device
- Send and execute time syncronized commands from host laptop to embedded device
- Use 3rd embedded platform to measure accuracy of timed event execution

## Approach
My approach to complete this project is broken up logically into the 2 parts. Create the detectiion and prediction pipeline. Then create and meaasure accuracy of the embedded platform event execution. Then integrate the predictions with the embedded platform execution. 


## Timeline
- Week 3: Installed and made BeatNet functional
- Week 4: Install and create a basic streaming pipeline in Essentia
- Week 5: Complete full essentia streaming pipeline with predictions and visualizations 
- **Week 6**: Begin work on embedded side, execute time synced events on an embedded platform
- Week 7: Midterm Checkpoint, create presentation and video of intial demo of integration of both parts. Update website and repo with progress
- Week 8: Finish integration and create accuracy testing framework.
- Week 9: Expand lighting side past triggering a single event. Make lighting more dynamic. 
- Week 10: Wrap up development into a polished product
- Finals Week: Create final presenation an demo. 
  