-- set minimum xmake version
set_xmakever("2.8.2")

-- includes
includes("lib/commonlibsf")

-- set project
set_project("StarfieldGalacticRadio")
set_version("1.2")
set_license("GPL-3.0")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- set policies
set_policy("package.requires_lock", true)

-- add rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")
set_config("mode", "releasedbg")

add_requires("toml11", "fmt")

-- setup targets
target("StarfieldGalacticRadio")
    -- add dependencies to target
    add_deps("commonlibsf")

    add_packages("toml11", "fmt")
    -- add commonlibsf plugin
    add_rules("commonlibsf.plugin", {
        name = "StarfieldGalacticRadio",
        author = "ChairGraveyard",
        description = "SFSE plugin for starfield",
        options = {
            address_library = true,
            layout_dependent = true
        }
    })

    add_defines("UNICODE")

    on_load(function(target)
      import("core.project.project")

      target:add("defines", "PROJECT_NAME=\"" .. project.name() .. "\"")
      target:add("defines", "PROJECT_VERSION=\"" .. project.version() .. "\"")
    end)

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    after_build(function(target)
      import("core.project.project")
      os.cp(path.join(target:targetdir(), project.name() .. ".dll"), "$(projectdir)/dist/Data/SFSE/Plugins/")
    end)