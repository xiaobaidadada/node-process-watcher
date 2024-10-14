const fs = require('fs');
const path = require('path');

// 删除指定目录下的所有文件
function deleteAllFilesInDirectory(directory) {
    fs.readdir(directory, (err, files) => {
        if (err) {
            console.error(`无法读取目录: ${err}`);
            return;
        }

        files.forEach(file => {
            const filePath = path.join(directory, file);

            // 检查是否为文件
            fs.stat(filePath, (err, stat) => {
                if (err) {
                    console.error(`无法获取文件信息: ${err}`);
                    return;
                }

                if (stat.isFile()) {
                    // 删除文件
                    fs.unlink(filePath, (err) => {
                        if (err) {
                            console.error(`无法删除文件: ${err}`);
                        } else {
                            console.log(`已删除文件: ${filePath}`);
                        }
                    });
                }
            });
        });
    });
}

// 调用函数删除某目录下所有文件
const directoryPath = '../c'; // 替换为你要删除文件的目录
deleteAllFilesInDirectory(directoryPath);
