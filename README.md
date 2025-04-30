# OpenXR Widescreen

This is an OpenXR layer that crops the field of view to the widescreen aspect ratio. Effectively disabling rendering of the top and bottom of the viewport which is especially useful for simracing where it just feels like wearing a helmet.

It has only been tested with a [BigScreenBeyond](https://www.bigscreenvr.com/), and is only enabled for iRacing. Using HWINFO I see a typical drop in GPU usage from 50% to 30% in Ring Meister practice when enabled, which leaves a good buffer for more demanding series.

If you are using SteamVR your better option is to use the built-in "Advanced Adjustment" tool. If you have a Meta Quest, your better option is to use the Oculus Debug Tool and modify the "FoV Tangent" values.

This is forked from an archived exemplar by Matthieu Bucchianeri (author of the discontinued [OpenXR-Toolkit](https://mbucchia.github.io/OpenXR-Toolkit/)), who outlined how to write this code.

If you like this and want to show your support, you can <a href="https://www.buymeacoffee.com/fommil"><img src="https://cdn.buymeacoffee.com/buttons/default-orange.png" alt="buy me a coffee" style="width:auto;height:2em;"></a>.

## Install

It's not entirely obvious, but github has a "releases" tab for this. Repeated for convenience: [Releases](https://github.com/fommil/openxr-widescreen/releases).

Download the `.msi` and double click it. You'll probably have to tell Windows, multiple times, that it's not a virus. If enough people donate, I might buy a digital certificate to try and avoid that, as well as keeping anti-cheat systems happy.

To uninstall, just use "Add / Remove Programs" in your windows Settings.

## Customise

You can create a file in `~/AppData/Local/XR_APILAYER_fommil_widescreen.ini` containing an alternative target aspect ratio:

```
[Settings]
aspect = 2.0 # more like ultra-wide
```

## Developers

To build it yourself, use Visual Studio and install C++, NuGet and WiX components. The `Debug` build is very chatty, so just use the `Release` one unless you really want to dig into the details.

The cleanest way to install is to use the WiX `Installer` package to create a `.msi` but you can also just build the `.dll`, put it in the same directory as the `.json` file, and run the following powerscript from that directory (or manage the registry manually).

```
$JsonPath = Join-Path "$PSScriptRoot" "XR_APILAYER_fommil_widescreen.json"
Start-Process -FilePath reg.exe -Verb RunAs -Wait -ArgumentList "ADD HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit /v `"$JsonPath`" /f /t REG_DWORD /d 0"
```

To uninstall, delete the registry entry. You can also set the value to `1` to "disable" it without uninstalling.
