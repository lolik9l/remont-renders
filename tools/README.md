# Рендер 3D-страницы ремонта

Код без данных квартиры. Сцена берётся из опубликованной 3D-страницы (Artifact read → page.html).

1. `python3 make_dbg.py page.html dbg.html` — отладочная копия страницы с `window.__walk` и `window.__dump`.
2. `python3 dump.py dbg.html scene kitchen 'kday:4.95,1.55,2.22,-2.75,-0.12,1.15'` — JSON сцены для камеры (x, высота глаз, z плана, yaw, pitch, fov). Хеш: kitchen, room, hall, bath.
3. `python3 convert.py scene_kday.json kday.bin`
4. `gcc -O3 -fopenmp -o pt pt.c -lm`, затем `./pt kday.bin out x y z yaw pitch fov eve spp w h` (eve 0 день, 1 вечер; 40 spp 1200×800 ≈ 4 мин на 2 ядрах).
5. `python3 post.py out.bin out.jpg ev` — тон-маппинг (ev 0 день, −0.8 вечер).

Камеры: кухня день (4.95,1.55,2.22,−2.75,−0.12,1.15), от холодильника (5.3,1.55,1.0,−0.96,−0.12,1.2), на окно (4.0,1.55,2.3,2.17,−0.1,1.15); комната на нишу от окна (4.3,1.5,5.1,−2.611,−0.04,1.2), прямо на нишу (2.6,1.5,4.35,3.1416,0,1.2); коридор от входа (0.6,1.55,0.15,0,−0.03,1.15).

Нейросеть: исходник кладётся в этот репозиторий, в image_urls идёт `https://raw.githubusercontent.com/lolik9l/remont-renders/main/<файл>`, после генерации файл удаляется.
