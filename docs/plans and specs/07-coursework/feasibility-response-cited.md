Full concession on my part that my pitch was...very limited in explanation of the scope and implementation of this project. This is a super multidisciplinary (and difficult to contextualize in a useful way for ENGINEERS in 60 seconds) project. While I absolutely recognize the importance of being able to pitch an idea to stakeholders at a high level in an extremely limited amount of time, it was decidedly impossible to DEMOSNTRATE the feasibility of this project and explain the intersections between physics, architecture, and impacts that I have drawn in planning. First I will respond directly to the feedback I received:

Something worth noting up front, none of this project will ACTUALLY involve the analog domain, this is a digital emulator of a proposed analog IC design [1][2][3][4][5]. I was not sure that made it across, and the non-idealities i mentioned are injected numerically, which makes them sweepable (you will see why that matters later)

Feedback received:

"If you're doing matrix operations in analog space, noise is going to add up very quickly"
- This has been recognized and considered in the design of this project, and makes up the entire project justification. This limits the possible array size and scalability as of right now [6][7], but the fact that "noise accumulates" is actually the whole point of the tool (which I will cover more in-depth later). The idea is to emulate the ferroelectric crossbars with injected nonidealities to see just how much noise the system can take BEFORE breaking down: investigate the impact of noise from device nonidealities on performance to give engineers a good idea of how precise their manufacturing tolerances need to be and how far they can push ICs with tolerances that have already been established. The question to answer is "how much non-ideality can the system take before it becomes useless for ML models," and that is a genuine utility for engineers of hardware accelerators without a background in material science.


"we assume that models trained in digital space aren't very resilient to noise"
- This has been addressed by IBM's Nature Communications study [8], and will be a part of the utility that FeCIM will provide. The practical implication of the accumulation of errors due to device non-idealities is "how much accuracy does a digitally-trained network lose, and which non-ideality DOMINATES that loss" [8][9][10]. This project would allow engineers to play with multiple device physics parameters and see how they impact accuracy on digitally-trained networks before ever implementing physical versions of those ICs [11].


"also, if you need more nodes you're fighting the physics of having more analog nodes which can't be easily made smaller as compared to digital nodes that can be (transistors can be very small)"
- This is a misunderstanding of the actual technology this emulator is based off. FeFETs ARE transistors, and are capable of the exact same footprint and physical scaling that standard MOSFETs are, the only important difference being the ferroelectric properties that the dielectrics exhibit [12][13]. I will drop some MSE papers on FeFETs at the bottom, but trust me bro FeFETs can scale just like any other transistor, if that were not the case, there would not be tons of research being done on them as potential backbones of compute-in-memory architectures [11][14]. What DOESN'T scale with FeFETs is the variability between devices: fewer ferroelectric domains per device (see pretty much any paper on ferroelectricity for an explanation) means more device-to-device variability, which is one of the primary tradeoffs this tool is built to quantify [7][11][13].



Additionally, I will have support from Dr. Juan Claudio Nino (the PI for my research lab), who runs experimentation on ferroelectric characterization (which is the work I do for the lab) and cofounded Rain Neuromorphics (now Rain AI), which I am pretty sure uses crossbars. Talking with him about neuromorphic architectures was what led me to want to pursue this project, and though he will not be directly involved in this project, he will be available and EXCEPTIONALLY useful for advice.


Additionally on


Long story short, yeah this is a really difficult, really in-depth, and fairly novel project. I also pinky promise I know what I am doing and all of my background in MSE and neuromorphic architecture and the resources I have through my PI and collaborators in my research lab give me what I believe to be enough credibility to get this done. Plus I already lowkey have 3 other people who heard that I was working on this and are prepared to abandon their own already approved pitches to work on it...which I recognize what kind of a hasty thing to do but they all said I should fight to get this approved so I will absolutely do that.

Some sources that Claude helped me compile:

FPGA emulation of compute-in-memory crossbars

[1] Accurate Emulation of Memristive Crossbar Arrays for In-Memory Computing. arXiv:2004.03073. https://arxiv.org/abs/2004.03073 — PCM cell and crossbar emulator on Kintex UltraScale; captures conductance drift and 1/f noise; validated against a ~400,000-device PCM prototype array.

[2] Wen, J., Vargas, F., Zhu, F., Reiser, D., Baroni, A., Fritscher, M., Perez, E., Reichenbach, M., Wenger, C., Krstić, M. RRAMulator: An efficient FPGA-based emulator for RRAM crossbar with device variability and energy consumption evaluation. Microelectronics Reliability, vol. 168, art. 115630, May 2025. doi:10.1016/j.microrel.2025.115630 — FPGA-based RRAM crossbar emulator incorporating device variability and energy consumption estimation.

[3] Reiser, D., Knödtel, J., Almeeva, L., Wen, J., Baroni, A., Krstić, M., Reichenbach, M. HyRPF: Hybrid RRAM Prototyping on FPGA. In Embedded Computer Systems: Architectures, Modeling, and Simulation (SAMOS 2024), pp. 199–215. doi:10.1007/978-3-031-78377-7_14 — Hybrid FPGA prototyping methodology for RRAM-based systems.

[4] GENIEx: A Generalized Approach to Emulating Non-Ideality in Memristive Xbars using Neural Networks. arXiv:2003.06902. https://arxiv.org/abs/2003.06902 — Neural-network-based emulation of crossbar non-idealities including IR drop and device non-linearity.

[5] A Decomposition-Based Memristive Crossbar Solver and FPGA-Accelerated Hardware Implementation. Proc. Great Lakes Symposium on VLSI (GLSVLSI) 2025. doi:10.1145/3716368.3735282 — FPGA-accelerated circuit-level solver for memristive crossbar arrays.

Noise accumulation, array scaling, and hardware-aware training

[6] Rapid yet accurate Tile-circuit and device modeling for Analog In-Memory Computing. arXiv:2506.00004. https://arxiv.org/abs/2506.00004 — Tile-circuit model covering IR drop, device noise, and ADC quantization; reports IR-drop severity (BERT to near-chance accuracy) and recovery via hardware-aware training plus per-column calibration.

[7] A 28-nm FeFET Compute-in-Memory Macro With 64×64 Array Size and On-Chip 4-Bit Flash ADC. — 4 kb macro in GlobalFoundries 28 nm HKMG; 64×64 array with eight 4-bit flash ADCs; examines charge trapping effects on retention and endurance.

[8] Hardware-aware training for large-scale and diverse deep learning inference workloads using in-memory computing-based accelerators. Nature Communications, 2023. https://www.nature.com/articles/s41467-023-40770-4 — Retraining to floating-point iso-accuracy across convnets, RNNs, and transformers; finds input/output non-idealities dominate weight non-idealities.

[9] Nair, L., Bunandar, D. Sensitivity-Aware Finetuning for Accuracy Recovery on Deep Learning Hardware. Lightmatter. arXiv:2306.03076. https://arxiv.org/abs/2306.03076 — Identifies noise-sensitive layers to accelerate noise-injection training.

[10] Improving the Accuracy of Analog-Based In-Memory Computing Accelerators Post-Training. arXiv:2401.09859. https://arxiv.org/abs/2401.09859 — Post-training methods for recovering accuracy on analog in-memory computing hardware.

FeFET crossbar demonstrations

[11] Soliman, T., Chatterjee, S., Laleni, N., et al. First demonstration of in-memory computing crossbar using multi-level Cell FeFET. Nature Communications, October 2023. https://www.nature.com/articles/s41467-023-42110-y — 1FeFET-1R multi-level cell; 96.6% handwriting / 91.5% image classification without extra training; 885 TOPS/W.

[12] Demonstration of Multiply-Accumulate Operation With 28 nm FeFET Crossbar Array. IEEE Electron Device Letters. — GlobalFoundries 28 nm HKMG; access transistors, current limiters, and current-mode ADC on the same wafer; full MAC yield on a 300 mm wafer; 8×8 segments.

[13] De, S., Müller, M., et al. 28 nm HKMG-Based Current Limited FeFET Crossbar-Array for Inference Application. IEEE Transactions on Electron Devices, 2022. — MLP inference with experimentally obtained device-to-device variation; retention degradation analysis.

[14] Memory Is All You Need: An Overview of Compute-in-Memory Architectures for Accelerating Large Language Model Inference. arXiv:2406.08413. https://arxiv.org/abs/2406.08413 — Survey of compute-in-memory architectures and hardware-aware training approaches.
