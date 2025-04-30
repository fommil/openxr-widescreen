# OpenXR Widescreen

This is an OpenXR layer that crops the field of view to the widescreen aspect ratio. Effectively disabling rendering of the top and bottom of the viewport. This reduces the computational overhead, which is especially useful for simracing where that region would be obscured by a helmet.

It has only been tested with a [BigScreenBeyond](https://www.bigscreenvr.com/), and is only enabled for iRacing. Using HWINFO I see a typical drop in GPU usage down from 50% to 30% when this layer is enabled, which I find to be a good base giving me overhead for full grids and rain.

This is forked from an archived exemplar by Matthieu Bucchianeri (the author of OpenXR-Toolkit), who helped me out.

If enough people use this and donate, I might buy a digital certificate to sign it and keep all the virus checkers and anti-cheat systems happy. Until then, users have to manually tell Windows that it's safe to download and install.

[![buy me a coffee](https://cdn.buymeacoffee.com/buttons/default-orange.png)](https://www.buymeacoffee.com/fommil)

... pssst! I've contributed a lot of code to [CrewChief](https://thecrewchief.org/)

## Customise

Create a file in `~/AppData/XR_APILAYER_fommil_widescreen.ini` containing an alternative target aspect ratio:

```
[Settings]
aspect = 2.0 # more like ultra-wide
```

## Developers

To build it yourself, use Visual Studio 2019 and install C++, NuGet and WiX components. The `Debug` build is very chatty, so just use the `Release` one unless you really want to dig into the details.

The cleanest way to install is to use the WiX `Installer` package to create a `.msi` but you can also just build the `.dll`, put it in the same directory as the `.json` file, and run the following powerscript from that directory (or manage the registry manually).

```
$JsonPath = Join-Path "$PSScriptRoot" "XR_APILAYER_fommil_widescreen.json"
Start-Process -FilePath reg.exe -Verb RunAs -Wait -ArgumentList "ADD HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit /v `"$JsonPath`" /f /t REG_DWORD /d 0"
```

To uninstall, run this or delete the registry entry. You can also set the value to `1` to "disable" it without uninstalling.

```
$JsonPath = Join-Path "$PSScriptRoot" "XR_APILAYER_fommil_widescreen.json"
Start-Process -FilePath reg.exe -Verb RunAs -Wait -ArgumentList "DELETE HKLM\Software\Khronos\OpenXR\1\ApiLayers\Implicit /v `"$JsonPath`" /f"
```
