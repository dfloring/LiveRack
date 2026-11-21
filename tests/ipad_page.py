# The iPad / web remote page in a real (headless) browser against a running
# LiveRack test build: live socket, meters, bypass, plugin view, edit mode.
# Uses LIVERACK_TEST_CHROME if set, otherwise Playwright's own Chromium.
import asyncio, os
from playwright.async_api import async_playwright
async def main():
    async with async_playwright() as p:
        chrome = os.environ.get("LIVERACK_TEST_CHROME")
        b = await p.chromium.launch(executable_path=chrome or None, args=["--no-sandbox"])
        pg = await b.new_page(viewport={"width": 1180, "height": 820})
        errors = []
        pg.on("pageerror", lambda e: errors.append(str(e)))
        await pg.goto("http://localhost:8090/")
        await pg.wait_for_timeout(1500)
        st = lambda: pg.evaluate("fetch('/state').then(r=>r.json())")
        print("WEB live socket open:", await pg.evaluate("live && live.readyState === 1"), "strips:", await pg.locator(".strip").count())
        w1 = await pg.evaluate("document.querySelector('[data-m=\"4\"]').style.width")
        await pg.wait_for_timeout(700)
        w2 = await pg.evaluate("document.querySelector('[data-m=\"4\"]').style.width")
        print("WEB meter widths", w1, w2, "light ok:", await pg.evaluate("document.getElementById('light').className"))
        await pg.locator("[data-bypass='1']").click(); await pg.wait_for_timeout(500)
        s = await st(); print("WEB bypass via live:", s["racks"][1]["bypass"], "button:", await pg.locator("[data-bypass='1']").inner_text())
        await pg.locator("[data-bypass='1']").click(); await pg.wait_for_timeout(500)
        # open a plugin's sliders (view query over live)
        await pg.locator("[data-open='0:1']").click(); await pg.wait_for_timeout(1200)
        print("WEB view over live:", await pg.evaluate("state.view && state.view.name"), "params", await pg.evaluate("state.view && state.view.params.length"),
              "plugin window shown:", await pg.evaluate("document.getElementById('gui').className"))
        # edit mode still lists shows/drivers over live
        await pg.goto("http://localhost:8090/"); await pg.wait_for_timeout(1200)
        await pg.click("#editBtn"); await pg.wait_for_timeout(800)
        print("WEB edit: add-plugin buttons", await pg.locator("[data-addp]").count(), "drivers in state", await pg.evaluate("state.drivers && state.drivers.length"))
        # The page's greys and accent must match Source/Theme.h (they drifted once, #22)
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        theme = open(os.path.join(root, "Source", "Theme.h")).read()
        import re
        def th(name): return "#" + re.search(r"Colour\s+" + name + r"\s*\{\s*0xff([0-9a-fA-F]{6})", theme).group(1).lower()
        want = {"--bg": th("background"), "--panel": th("panel"), "--raised": th("raised"), "--border": th("border"), "--accent": th("accent")}
        got = {k: (await pg.evaluate("getComputedStyle(document.documentElement).getPropertyValue('%s')" % k)).strip().lower() for k in want}
        print("WEB theme matches Theme.h:", got == want, "" if got == want else (got, want))
        out = os.environ.get("LIVERACK_TEST_OUT")
        if out:
            await pg.screenshot(path=os.path.join(out, "ipad-page.png"))
        print("WEB errors:", errors)
        await b.close()
asyncio.run(main())
