# minify_gzip.py (v4 包含原始文件删除逻辑)
# PlatformIO extra_script to minify and gzip web files before building the filesystem image.

import os
import gzip
import shutil

# 确保 env 变量在脚本的全局作用域中可用，尤其是在早期的错误处理中。
Import('env') # type: ignore  <-- ，告诉Pylance忽略此行

# --- 依赖检查 ---
# 检查新库是否已安装
try:
    import minify_html
    import rcssmin
    import rjsmin
except ImportError:
    # 此时 env 变量已经可用
    print("\n[minify_gzip.py] 错误：缺少必要的Python库。")
    print("请在您的Python环境中通过pip安装它们:")
    print("pip install minify-html rcssmin rjsmin")
    env.Exit(1)

def minify_and_gzip_web_files(source, target, env):
    """
    此函数由PlatformIO在构建文件系统前调用。
    它会遍历data_dir，缩小HTML/CSS/JS，并Gzip所有文件。
    处理后，原始文件将被删除，只保留.gz文件。
    """
    data_dir = env.get("PROJECT_DATA_DIR")
    if not os.path.isdir(data_dir):
        print(f"\n[minify_gzip.py] '{data_dir}' 目录不存在，跳过处理。")
        return
        
    print(f"\n[minify_gzip.py] 开始处理 '{data_dir}' 目录中的Web资源...")

    # 用于存储需要删除的原始文件路径
    # 我们先收集所有要删除的文件，在主循环结束后再统一删除，
    # 避免在迭代过程中修改正在遍历的目录结构。
    files_to_delete = [] 

    for root, dirs, files in os.walk(data_dir):
        for file in files:
            file_path = os.path.join(root, file)
            file_ext = os.path.splitext(file)[1].lower()
            
            # 跳过已经Gzip的文件，避免重复处理和无限添加.gz扩展名
            if file_ext == '.gz':
                print(f"  -> 跳过已Gzip文件: {os.path.relpath(file_path, data_dir)}")
                continue

            print(f"  -> 正在处理: {os.path.relpath(file_path, data_dir)}")
            
            try:
                with open(file_path, "rb") as f_in:
                    content_bytes = f_in.read()
            except Exception as e:
                print(f"     [错误] 读取文件失败: {e}")
                continue

            minified_content_bytes = None
            is_minified = False

            # 仅对特定类型的文件进行缩小操作
            if file_ext in ('.html', '.css', '.js'):
                try:
                    if file_ext == '.html':
                        # minify_html 默认输入字符串，输出字符串，需要编解码
                        minified_content_bytes = minify_html.minify(
                            content_bytes.decode('utf-8'), 
                            minify_js=True, 
                            minify_css=True
                        ).encode('utf-8')
                    elif file_ext == '.css':
                        # rcssmin 和 rjsmin 期望输入字符串，需要编解码
                        minified_content_bytes = rcssmin.cssmin(content_bytes.decode('utf-8')).encode('utf-8')
                    elif file_ext == '.js':
                        minified_content_bytes = rjsmin.jsmin(content_bytes.decode('utf-8')).encode('utf-8')
                    is_minified = True
                except Exception as e:
                    print(f"     [警告] 缩小文件 '{os.path.basename(file_path)}' 时出错: {e}. 将使用原始文件进行Gzip压缩。")
                    minified_content_bytes = None # 出错则回退

            # 如果没有缩小成功，或者该文件类型不需要缩小，则使用原始内容进行Gzip
            content_to_compress = minified_content_bytes if minified_content_bytes else content_bytes
            
            gzipped_file_path = file_path + ".gz"
            try:
                # compresslevel=9 提供最高压缩比，但会增加处理时间
                with gzip.open(gzipped_file_path, "wb", compresslevel=9) as f_out:
                    f_out.write(content_to_compress)
                
                if is_minified:
                    print(f"     [成功] 已缩小并Gzip压缩至: {os.path.basename(gzipped_file_path)}")
                else:
                    print(f"     [成功] 已Gzip压缩至: {os.path.basename(gzipped_file_path)} (未缩小)")
                
                # 成功处理并Gzip后，将原始文件路径添加到待删除列表
                files_to_delete.append(file_path)

            except Exception as e:
                print(f"     [错误] Gzip压缩文件 '{os.path.basename(file_path)}' 失败: {e}")

    # 在所有文件处理完毕后，统一删除原始文件
    for file_to_del in files_to_delete:
        try:
            os.remove(file_to_del)
            print(f"  -> 已删除原始文件: {os.path.relpath(file_to_del, data_dir)}")
        except Exception as e:
            print(f"     [错误] 删除原始文件 '{os.path.basename(file_to_del)}' 失败: {e}")

    print("[minify_gzip.py] Web资源处理完成。\n")

# 将函数注册到PlatformIO的buildfs操作之前执行
env.AddPreAction("buildfs", minify_and_gzip_web_files)