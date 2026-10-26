# dump.py — снимает сцену отладочной страницы для трассировщика.
# Использование: python3 dump.py dbg.html out_prefix room 'name:x,y,z,yaw,pitch,fov' [...]
#   room — хеш помещения (kitchen, room, hall, bath); x,z — план в метрах, y — высота глаз, yaw: направление (sin yaw, cos yaw) в плоскости X,Y плана
# Потом: python3 convert.py out_prefix_name.json scene.bin ; ./pt scene.bin out x y z yaw pitch fov eve spp w h ; python3 post.py out.bin out.jpg ev
import asyncio, sys
from playwright.async_api import async_playwright
async def main():
    page,prefix,room=sys.argv[1],sys.argv[2],sys.argv[3]
    cams=[(a.split(':')[0],[float(v) for v in a.split(':')[1].split(',')]) for a in sys.argv[4:]]
    async with async_playwright() as p:
        b=await p.chromium.launch(args=['--use-angle=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist'])
        pg=await b.new_page(viewport={'width':1200,'height':800})
        await pg.route('**/fonts.googleapis.com/**', lambda r: r.abort())
        import os; await pg.goto('file://'+os.path.abspath(page)+'#'+room); await pg.wait_for_timeout(3000)
        for k,c in cams:
            await pg.evaluate(f'window.__walk({c[0]},{c[2]},{c[3]},{c[4]},{c[5]})'); await pg.wait_for_timeout(500)
            d=await pg.evaluate('window.__dump()')
            open(f'{prefix}_{k}.json','w').write(d); print(k,len(d))
        await b.close()
asyncio.run(main())
