<h1 align="center">🛠 Contributing to obs-goome 🛠</h1>

<p align="center">
Thanks for wanting to help! Small PRs that fix one thing are the easiest for me to check and merge, so keep them small and theyll get in way faster.<br>
This repo is only the OBS plugin. If the bug is in goome itself (drawing, a backend, the ui kit) it goes to <a href="https://github.com/goome-ui/c-goome">c-goome</a> instead, docs fixes go to <a href="https://github.com/goome-ui/docs">goome-ui/docs</a> (thats <a href="https://goome-ui.github.io/docs/">goome-ui.github.io/docs</a>) and new example projects to <a href="https://github.com/goome-ui/examples">goome-ui/examples</a>.
</p>

<details>
<summary align="center"><b>📋 The short version 📋</b></summary>

<p align="center">
• One thing per PR. One fix or one feature, if it does two things it gets closed<br>
• Open your PR against the <b>dev</b> branch, not main<br>
• Never touch <code>native/</code>, goome.h in there is copied from c-goome<br>
• Test it in a real OBS and say which OBS version and OS<br>
• Dont touch the protected files, a bot closes the PR if you do<br>
• Run <code>tests/run.sh check</code> and say how else you tested it<br>
• CI has to pass<br>
• AI is fine, but say you used it, and you need to know what every line does
</p>
</details>

<details>
<summary align="center"><b>💬 Before you start 💬</b></summary>

<p align="center">
Small fixes can go straight to a PR.<br>
For a new feature or anything big, <a href="https://github.com/goome-ui/obs-goome/issues">open an issue</a> first so we can talk about it before you spend a weekend on it.<br> <br>
Same if you want to rename something or change what a function takes or returns, people already use this so I want to talk that through first.
</p>
</details>

<details>
<summary align="center"><b>🌿 Branches 🌿</b></summary>

<p align="center">
Stuff goes dev, then beta, then main. I move it along, you only ever PR into <b>dev</b>.
</p>
</details>

<details>
<summary align="center"><b>🚫 What gets closed automatically 🚫</b></summary>

<p align="center">
A bot checks every PR and closes it with a comment if it hits any of these, fix it and open a new one.<br> <br>
It has to be against <b>dev</b>, main and beta only get stuff that already sat on dev for a while.<br> <br>
It cant touch <code>.github/</code>, since thats CI and these rules and a PR could just turn them off for itself. <code>native/</code> is off limits too, <code>goome.h</code> in there gets copied from c-goome by <code>tools/update_native.sh</code>. Same for <code>CMakeLists.txt</code>, <code>CMakePresets.json</code>, <code>buildspec.json</code>, <code>cmake/</code> and <code>tools/</code>, thats what builds the plugin everyone installs into OBS. The <code>LICENSE</code> and the screenshots in <code>docs/</code> stay mine to change.<br> <br>
• <b>More than 3000 changed files</b> - way too big to check<br> <br>
Renaming or moving any of those files counts too. Think one of them is wrong? <a href="https://github.com/goome-ui/obs-goome/issues">Open an issue</a> and I'll fix it.
</p>
</details>

<details>
<summary align="center"><b>🤖 Using AI 🤖</b></summary>

<p align="center">
Using AI to help write your PR is allowed, as long as you say so in the PR and you know what your code does. If you get asked about something you should be able to answer without going back to the AI.<br> <br>
And test it yourself. Untested AI dumps get closed
</p>
</details>

<details>
<summary align="center"><b>🧪 Testing 🧪</b></summary>

<p align="center">
The harness builds the real source code against a fake libobs and checks a pixel hash for every widget, nothing shows up on your screen:
</p>

```sh
tests/run.sh check
```

<p align="center">
On Windows run it from an msys2 MINGW64 shell with gcc. <code>CC</code> picks another compiler.<br> <br>
If you changed how something draws on purpose, run <code>tests/run.sh print</code> and update <code>tests/hashes.txt</code>, then say in the PR which hashes moved and why. <code>tests/run.sh shots</code> writes a .ppm of every render so you can look at them, put a before and after in the PR.<br> <br>
Anything the harness cant see (the properties window, Interact, how it looks in a real scene) needs a test in a real OBS. Write which OBS version and OS you tried.<br> <br>
CI builds the plugin on linux, windows and mac, runs the harness on all 3 and loads the plugin into a real libobs on linux. It has to be green before I merge.
</p>
</details>

<details>
<summary align="center"><b>✏️ Code style ✏️</b></summary>

<p align="center">
• C99 for everything in <code>src/</code> besides <code>goome-source.c</code> and <code>plugin-main.c</code>, those can use what libobs needs<br>
• No dependencies besides libobs and goome.h. No Qt, no frontend api<br>
• <code>overlay.c</code>, <code>builder.c</code> and <code>json.c</code> dont include any OBS header, the harness and other hosts use them as is<br>
• Nothing blocks the graphics thread. Files get read at most once a second<br>
• New settings go in <code>read_settings</code>, <code>src_defaults</code>, <code>src_properties</code> and <code>data/locale/en-US.ini</code><br>
• Match the code around it. snake_case, short names are fine inside functions<br>
• No comments unless something really isnt obvious<br>
• Dont reformat whole files, it makes the diff impossible to read
</p>
</details>

<details>
<summary align="center"><b>💬 Discussions and first issues 💬</b></summary>
<p align="center">
Questions and stuff you made with goome go in <a href="https://github.com/goome-ui/c-goome/discussions">Discussions</a>.<br>
If you want something to work on, the <a href="https://github.com/goome-ui/obs-goome/labels/good%20first%20issue">good first issue</a> ones are small. Comment on one before you start.
</p>
</details>

<p align="center">
Found a security hole? Dont open a public issue or PR for it, read <a href="https://github.com/goome-ui/.github/blob/main/SECURITY.md">SECURITY.md</a> and tell me privately first.<br> <br>
By sending a PR you agree your code goes out under the same <a href="LICENSE">GPL-3.0 license</a> as the rest of obs-goome.
</p>
