---
title: "How to use a specific version of MSVC in GitHub Actions"
layout: post
permalink: /MsvcGha/
tags: [ misc, _hidden ]
---

Alright, I'm not breaking new ground here, but this is a difficulty I've had, and maybe it's a difficulty you've had too. It's not common, but if you want to test your code with a particular version of MSVC, it's fairly tricky and finicky. Before sitting down this week to figure it out, I've never had success with running multiple versions of MSVC with GitHub Actions.

GitHub Actions used to have multiple versions of Visual Studio build tools installed on their Windows runners, but [this was removed in May 2024](https://github.com/actions/runner-images/issues/9701). Instead, only the latest build tools are present, so we must find a way to install whichever specific version of MSVC ourselves.

<!--more-->

After figuring out the process and starting to write this article, I found [setup-msvc-dev](https://github.com/marketplace/actions/setup-msvc-developer-command-prompt), an Action that claims to do exactly this. I haven't used it myself, so I can't speak on how well it works. This article only focuses on how I'm implementing this functionality for myself, and how you can too.

<br/>

## Cutting to the chase

Here's the general method. We need to install the compiler and build tools through the command line alone, and then ensure the environment is setup properly. These are the setups I devised, and I'll go into more detail on each one.

1. Download the correct bootstrapper
2. Execute the bootstrapper on quiet mode
3. Wait until the installer finishes
4. Run the batch script to set the correct env variables
5. Build as normal

<br/>

## 1. Download the correct bootstrapper

You can grab the version-specific and channel-specific bootstrappers from the [Visual Studio 2022 Release History page](https://learn.microsoft.com/en-us/visualstudio/releases/2022/release-history). They have a list of every patch version that has been released, and the associated bootstrapper executables. The "Build Tools" bootstrappers are the ones that don't require a license. As of right now, there is a similar page for Visual Studio [2019](https://learn.microsoft.com/en-us/visualstudio/releases/2019/history) and [2026](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-history), but I'm unsure about 2017, and hopefully there's no need for 2015 anymore.

I'm concerned that someone at Microsoft may decide to remove the publicly available download locations of the bootstrappers while we still need them. It doesn't seem too robust to rely on them keeping the bootstrappers up forever, but it's also more inconvenient to setup some sort of artifact repository with all the bootstrappers we might want. I plan to locally download all the bootstrappers I use to my own machine as an archive, and use Microsoft's download locations in the script. I'll deal with it later if the download location is removed.

On my machine locally, I have `wget` but I don't have `curl`. It's the opposite on GitHub Actions. Note than when using a "composite action" in GitHub Actions, you need to specify `curl.exe`. Use whichever download tool suits you.

For this demonstration, let's say I want to use Visual Studio 17.13.3. I'm choosing this because (1) 17.13 was never an LTSC, (2) 17.13 is out of support, and (3) this version isn't even the last patch of 17.13. If this version works, then anything should work.

```powershell
wget -O vs_buildtools.exe https://download.visualstudio.microsoft.com/download/pr/9b2a4ec4-2233-4550-bb74-4e7facba2e03/00f873e49619fc73dedb5f577e52c1419d058b9bf2d0d3f6a09d4c05058c3f79/vs_BuildTools.exe
# or
curl.exe -L -o vs_buildtools.exe https://download.visualstudio.microsoft.com/download/pr/9b2a4ec4-2233-4550-bb74-4e7facba2e03/00f873e49619fc73dedb5f577e52c1419d058b9bf2d0d3f6a09d4c05058c3f79/vs_BuildTools.exe
```

<br/>

## 2. Execute the bootstrapper on quiet mode

I found that `--quiet --norestart` worked well locally, which ensures that no UI elements pop up and the machine doesn't need to restart. However, the bootstrapper must be run as an administrator, which means there's a UI pop-up to approve running it as an admin. On GitHub Actions, we're already an admin, so we don't need to worry about that.

The `--installPath` can be set to anything. At the time of writing this article, the Windows runners on GHA use default working directory `D:\a\<repo-name>\<repo-name>`, which is where the repo's contents go once the code has been checked out. I will use `..\vs-install` as the install path.

The VS installer has a zillion "individual components" that you can install, which are grouped together into "workloads". [Giving it a cursory look](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools), we would want to use `Microsoft.VisualStudio.Workload.VCTools`, which is the ID for the workload "Desktop development with C++", no matter which version is being installed. That said, it installs many things that we may not need. Instead, I would rather focus on the individual components required for simply building C++. In this case for Visual Studio 17.13.3, the component for the compiler has the ID `Microsoft.VisualStudio.Component.VC.14.43.17.13.x86.x64`. We also need `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` for the batch script that sets up the environment variables. All in all, this amounts to the arguments `--add Microsoft.VisualStudio.Component.VC.14.43.17.13.x86.x64 --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64`. These components may have dependencies, so we also use `--includeRecommended`. In my experience, using the individual components cuts the install size in half, compared to the workload.

Lastly, as far as I'm aware, `--noUpdateInstaller` ensures that we use the exact version of the bootstrapper and installer that we want, and nothing newer. We may not need this argument, but I'd rather have it just in case.

<br/>

## 3. Wait until the installer finishes

Now we need to run the bootstrapper.

```powershell
.\vs_buildtools.exe `
  --quiet --norestart `
  --installPath ..\vs-install `
  --add Microsoft.VisualStudio.Component.VC.14.43.17.13.x86.x64 `
  --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  --includeRecommended `
  --noUpdateInstaller
```

However, this doesn't work. The bootstrapper process immediately exits successfully, while the installation happens in the background. While the bootstrapper is called `vs_buildtools.exe`, the installer is called `setup.exe`, and multiple of these may be spawned as part of the installation. Locally, I wrote a loop to wait until there are no more processes called "setup", but this didn't work on GHA.

After working on other sorts of "hackier" ideas like the manual looping, I finally settled on using something more robust. PowerShell's [`Start-Process`](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/start-process) command has a `-Wait` optional parameter, which does exactly what I need.

> Indicates that this cmdlet waits for the specified process and its descendants to complete before accepting more input. This parameter suppresses the command prompt or retains the window until the processes finish.

I'm also using the [`Start-Process`](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/start-process) argument `-NoNewWindow` to keep this process running in the current console.

> Start the new process in the current console window.

All told, we actually need to run the bootstrapper like this.

```powershell
Start-Process `
  -FilePath ".\vs_buildtools.exe" `
  -ArgumentList @(
    '--quiet', '--norestart',
    '--installPath', '..\vs-install',
    '--add', 'Microsoft.VisualStudio.Component.VC.14.43.17.13.x86.x64',
    '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    '--includeRecommended',
    '--noUpdateInstaller'
  ) `
  -NoNewWindow `
  -Wait
```

It doesn't look too different than the directly run command, but now the shell waits until the installer is finished, without doing something hacky like looping on which processes exist on the machine. While this is pretty simple after already knowing the solution, it took a while for me to get here.

<br/>

## 4. Run the batch script to set the correct env variables

I found this to be the most finicky part of this whole experience, and the most annoying to deal with.

If you install a version of MSVC, you can't just add the cmake option `-DCMAKE_CXX_COMPILER=path\to\cl.exe`, you need to setup the proper environment. Of course the environment variables can be set manually, but these may change across various versions. Instead, we use `<install-path>\VC\Auxiliary\Build\vcvarsall.bat`. This is a parametrized script that sets up the proper environment. You can either call `vcvarsall.bat x64` or use the provided script `vcvars64.bat` with no arguments, which calls `vcvarsall.bat` under the hood anyway.

If we simply run `..\vs-install\VC\Auxiliary\Build\vcvars64.bat`, the environment variables will only be set for the duration of the script, and will be reset afterwards. So we either need to find a way to make these environment variables escape the confines of the script, or we run our commands from within the context of the script.

First I tried the latter idea, with something like this.

```powershell
cmd /c "..\vs-install\VC\Auxiliary\Build\vcvars64.bat && powershell"
```

This starts a new PowerShell session within the session of `cmd` that's running the script, which itself is within our original PowerShell session. Shell-ception I guess. This works locally, but it doesn't work on GitHub Actions, and I'm not sure why. Next I tried figuring out other ways to call the script. Maybe using the `&` operator in PowerShell? Or using the `call` operator in `cmd`? None of those things allowed successfully starting the new PowerShell session with all the relevant environment variables set.

Instead, maybe it would work to call `cmake` from within the `cmd` session. Realistically, we don't actually need _everything_ to be within the proper MSVC environment. We only need the `cmake` generating step to have the correct environment, and then `cmake --build` can be run without it.

```powershell
cmd /c "..\vs-install\VC\Auxiliary\Build\vcvars64.bat && cmake <args...>"
```

This also worked on my local machine and didn't work in GHA. Honestly, I would love to know why, but I gave up on this train of thought and switched gears.

What if the environment variables could be captured from the script, and then set in the main PowerShell session? The key here is to silence the script's own output with [`>nul`](https://ss64.com/nt/nul.html), and then use the [`set`](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/set_1) command to display all the currently set environment variables.

```powershell
cmd /c "..\vs-install\VC\Auxiliary\Build\vcvars64.bat >nul && set"
```

This outputs all the environment variables from the script's context as pairs of `key=value`. We can pipe this into [`ForEach-Object`](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/foreach-object) and extract the relevant key-value pairs.

```powershell
cmd /c "..\vs-install\VC\Auxiliary\Build\vcvars64.bat >nul && set" |
  ForEach-Object {
    # ...
  }
```

From here, we could do a few different things. We could set the environment variables in the current PowerShell session, but that won't work with GitHub Actions. With GHA, each step uses a new shell session, so the environment variables will be lost later. Instead, we can use the [`GITHUB_ENV` environment variable](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-commands#setting-an-environment-variable) to pass our environment variables between steps, and keep these changes for the duration of the job.

In Bash, this would look like `echo "MY_ENV_VAR=myValue" >> $GITHUB_ENV`, and this is the format of all the examples on the GHA docs. Using PowerShell, it looks like `Add-Content $env:GITHUB_ENV "MY_ENV_VAR=myValue"`.

Each iteration passed to `ForEach-Object` is already of the form `MY_ENV_VAR=myValue`, therefore here is the final command to the batch script.

```powershell
cmd /c "..\vs-install\VC\Auxiliary\Build\vcvars64.bat >nul && set" |
  ForEach-Object {
    Add-Content $env:GITHUB_ENV $_
  }
```

After this, all the subsequent steps in the job will still have the proper MSVC environment setup, so there is no need for starting inner PowerShell sessions or anything like that. This method is generic enough to work on any MSVC version, as of the time of writing this article.

<br/>

## 5. Build as normal

After the previous steps have all been wrapped up into a composite GitHub Action, and they've been appropriately parametrized, we can just build as normal. If the previous steps have succeeded, then this will build the project with the desired version of MSVC.

For example, I've been testing with a GHA script that looks similar to this.

```yaml
on:
  push:
jobs:
  install-msvc:
    runs-on: windows-2022
    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Setup MSVC
        uses: ./.github/actions/setup-msvc
        with:
          vs-version: "14.43.17.13"
          bootstrapper-url: "https://download.visualstudio.microsoft.com/download/pr/9b2a4ec4-2233-4550-bb74-4e7facba2e03/00f873e49619fc73dedb5f577e52c1419d058b9bf2d0d3f6a09d4c05058c3f79/vs_BuildTools.exe"
          # install-path: ..\vs-install # Defaults to this value

      - name: Build the code
        run: |
          mkdir build
          cd build
          cmake .. -G "Visual Studio 17 2022"
          cmake --build . --target main
          & .\build\Debug\main.exe
```

In this case, I created a small executable that just spits out the MSVC version.

```cpp
#include <iostream>
int main() {
    std::cout << "MSVC version " << _MSC_FULL_VER << '\n';
}
```

With the parameters given to the composite action in this article, I get the following output.

```none
MSVC version 194334809
```

MSVC 19.43.34809 is the version shipped with Visual Studio 17.13.3, so it looks like this all works!

<br/>

## A note on CMake compatibility

You may get the wrong version of Visual Studio if you don't specify the correct generator to CMake. For example, if you want to install and use a version of Visual Studio 2019, you should add the generator `-G "Visual Studio 16 2019"` to your CMake command. This bit me a few times in testing.

Also note that the [`"Visual Studio 18 2026"` generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html) was only added in CMake 4.2, in [November 2025](https://github.com/Kitware/CMake/releases/tag/v4.2.0). At the time of writing, the [GitHub-hosted Windows runners](https://github.com/actions/runner-images) don't yet have CMake 4.2. If you want to use CMake with Visual Studio 2026 at the time of writing, then you will likely also need to download CMake >=4.2. That's outside the scope of this article and my script.

If you aren't using CMake, then everything should be fine. The environment variables are set successfully, and `cl.exe` calls the correct compiler. I've tested this with 2019, 2022, and 2026.

This section of this article will hopefully become outdated very quickly. Although, in the future, the same thing might happen with the next version after 2026 anyway.

<br/>

## Wait, are we re-installing these Visual Studio components every single time?

Yeah, unfortunately. I tried to use `actions/cache@v4` on the install directory, but it didn't work. If the cache doesn't yet exist, then everything works just fine. If the cache exists already, CMake detects the pre-installed MSVC version instead of the one installed in the script. At the time of writing this article, I'm getting `MSVC version 194435222` instead, regardless of which version I installed.

At this time, I haven't been able to figure out why that's happening. I'd rather get this script out into the world sooner, and worry about the caching optimization later.

I'd appreciate any help on this front, if you are reading this and you see an obvious solution.

<br/>

## Parametrizing this whole thing

At first I wrote this article and the accompanying GitHub Action such that the Action was parametrized on the installer component ID and the build tools bootstrapper URL. I decided that was a bad user experience, so I changed it. Now, the Action has a Python script that scrapes the "Release History" pages and grabs all the URLs, and then validates the input version based on what does or doesn't exist on those webpages.

This makes for a much easier user experience. Now, for version `17.13.3`, you specify `17.13.3`. No need to specify `14.43.17.13` and go hunting for the bootstrapper URL.

This also means I can accept things like `17.13`, and automatically use the latest patch version for this minor version. In this case, we get `17.13.7`.

<br/>

## You can use this Action!

So that's it. That's how to use any MSVC version with GitHub Actions. It's nice to have this wrapped up in a pre-packaged composite Action, and then hopefully never worry about it again.

If you want to use this action, you can take a look at [k3DW/setup-msvc](https://github.com/k3DW/setup-msvc). It's very easy to use, you only need to specify `vs-version: "major.minor[.patch]"`. Check out the repo for more details.

At the time of writing, it looks like the following. This may change in future versions.

```yml
- name: Setup MSVC
  uses: k3DW/setup-msvc@v1
  with:
    vs-version: "17.13.3"
```

And of course, I'm happy to discuss this more with anyone who might be interested. Thanks for reading!
