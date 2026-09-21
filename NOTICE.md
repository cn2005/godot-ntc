# Third-party notices

This repository is **not** an official NVIDIA product and is not sponsored or
endorsed by NVIDIA.

## Godot NTC (this repository)

Original C++ GDExtension sources, GDScript editor plugins, and documentation in
this repository are licensed under the MIT License. See [LICENSE](LICENSE).

## NVIDIA RTX Neural Texture Compression

Runtime decoding and editor compression call **LibNTC** from the
[NVIDIA RTX NTC SDK](https://github.com/NVIDIA-RTX/RTXNTC) (tested with v0.10.0
BETA). LibNTC and the NTC file format are governed by the NVIDIA RTX SDKs
license, not by MIT.

You must obtain the SDK yourself, accept NVIDIA's terms, and keep those terms
at least as protective when you redistribute `libntc.dll` or other SDK
binaries as part of an application. Do **not** distribute the NVIDIA SDK as a
stand-alone product.

The NVIDIA RTX SDKs license text shipped with LibNTC is copied below for
convenience. The copy in your SDK install is authoritative.

---

NVIDIA RTX SDKs LICENSE

This license is a legal agreement between you and NVIDIA Corporation ("NVIDIA") and governs the use of the NVIDIA RTX
software development kits, including the DLSS SDK, NGX SDK, RTXGI SDK, RTXDI SDK, RTX Video SDK and/or NRD SDK, if and
when made available to you under this license (in each case, the "SDK").
This license can be accepted only by an adult of legal age of majority in the country in which the SDK is used. If you are
under the legal age of majority, you must ask your parent or legal guardian to consent to this license. If you are entering this
license on behalf of a company or other legal entity, you represent that you have legal authority and "you" will mean the
entity you represent.
By using the SDK, you affirm that you have reached the legal age of majority, you accept the terms of this license, and you
take legal and financial responsibility for the actions of your permitted users.

The full license text is in the RTX NTC SDK at `libraries/RTXNTC-Library/LICENSE.txt`.
Read it before redistributing LibNTC binaries.

## godot-cpp

The GDExtension is built against [godot-cpp](https://github.com/godotengine/godot-cpp)
(MIT). Clone it into `thirdparty/godot-cpp`; it is not vendored in this
repository.

## Demo textures

`demo/assets/MetalPlates013` is derived from
[MetalPlates013](https://ambientcg.com/view?id=MetalPlates013) on ambientCG
(CC0 1.0). Additional compare materials under `demo/assets/pbr4k` are also
from ambientCG (CC0 1.0) and are not committed.
