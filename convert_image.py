#!/usr/bin/env python3
"""
LVGL 图片转换脚本
将 JPG/PNG 图片转换为 RGB565 格式的 C 数组
"""

import sys
from PIL import Image

def convert_image(input_file, output_file, var_name, width=320, height=240):
    """将图片转换为 LVGL RGB565 格式的 C 数组"""

    # 打开图片
    img = Image.open(input_file)

    # 调整大小
    img = img.resize((width, height), Image.Resampling.LANCZOS)

    # 转换为 RGB
    img = img.convert('RGB')

    # 准备数据
    data = []
    for y in range(height):
        for x in range(width):
            r, g, b = img.getpixel((x, y))

            # 转换为 RGB565
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F

            # 组合为 16 位值
            rgb565 = (r5 << 11) | (g6 << 5) | b5

            # 转换为字节（小端序）
            data.append(rgb565 & 0xFF)
            data.append((rgb565 >> 8) & 0xFF)

    # 生成 C 文件
    with open(output_file, 'w') as f:
        f.write('#include "lvgl.h"\n\n')
        f.write('#ifndef LV_ATTRIBUTE_MEM_ALIGN\n')
        f.write('#define LV_ATTRIBUTE_MEM_ALIGN\n')
        f.write('#endif\n\n')

        # 写入数组
        f.write(f'const LV_ATTRIBUTE_MEM_ALIGN uint8_t {var_name}_map[] = {{\n')

        # 每行 16 个字节
        for i in range(0, len(data), 16):
            line = data[i:i+16]
            hex_values = ', '.join(f'0x{b:02X}' for b in line)
            f.write(f'    {hex_values},\n')

        f.write('};\n\n')

        # 写入描述符
        f.write(f'const lv_image_dsc_t {var_name} = {{\n')
        f.write(f'    .header.magic = LV_IMAGE_HEADER_MAGIC,\n')
        f.write(f'    .header.cf = LV_COLOR_FORMAT_RGB565,\n')
        f.write(f'    .header.w = {width},\n')
        f.write(f'    .header.h = {height},\n')
        f.write(f'    .data_size = sizeof({var_name}_map),\n')
        f.write(f'    .data = {var_name}_map,\n')
        f.write('};\n')

    print(f"转换完成: {output_file}")
    print(f"图片尺寸: {width}x{height}")
    print(f"数据大小: {len(data)} bytes")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("用法: python convert_image.py <input.jpg> <output.c> [var_name]")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    # 从输出文件名生成变量名
    if len(sys.argv) > 3:
        var_name = sys.argv[3]
    else:
        # 从文件名生成变量名，例如 photo_02.c -> photo_02
        var_name = output_file.split('/')[-1].split('\\')[-1].replace('.c', '')

    convert_image(input_file, output_file, var_name)