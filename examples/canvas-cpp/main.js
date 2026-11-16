(function() {
    console.log("合并版 Canvas Demo - 强制全屏自适应");

    // 等待 DOM 就绪
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

    function init() {
        // 创建 canvas
        var canvas = document.createElement('canvas');
        document.body.appendChild(canvas);

        // ========== 关键：CSS 强制填满屏幕 ==========
        canvas.style.position = 'fixed';
        canvas.style.top = '0';
        canvas.style.left = '0';
        canvas.style.width = '100%';
        canvas.style.height = '100%';
        canvas.style.display = 'block';
        canvas.style.margin = '0';
        canvas.style.padding = '0';
        document.body.style.margin = '0';
        document.body.style.padding = '0';
        document.body.style.overflow = 'hidden';

        var ctx = canvas.getContext('2d');
        if (!ctx) {
            console.error("无法获取 2d 上下文");
            return;
        }

        // 鼠标/触摸状态
        var mouse = { x: 0, y: 0, down: false, clicks: 0 };
        var img = new Image();
        var imgLoaded = false;
        var imgError = false;

        img.onload = function() { imgLoaded = true; console.log("图片加载成功"); };
        img.onerror = function() { imgError = true; console.error("图片加载失败"); };
        img.src = "logo.png";

        // 主动同步画布像素尺寸与窗口尺寸
        function syncCanvasSize() {
            var w = window.innerWidth;
            var h = window.innerHeight;
            if (canvas.width !== w || canvas.height !== h) {
                canvas.width = w;
                canvas.height = h;
                console.log("画布尺寸同步: " + w + " x " + h);
            }
        }

        // 坐标转换（考虑 CSS 缩放）
        function getMouseCoord(e) {
            var rect = canvas.getBoundingClientRect();
            var scaleX = canvas.width / rect.width;
            var scaleY = canvas.height / rect.height;
            var x = (e.clientX - rect.left) * scaleX;
            var y = (e.clientY - rect.top) * scaleY;
            x = Math.min(Math.max(0, x), canvas.width);
            y = Math.min(Math.max(0, y), canvas.height);
            return { x: x, y: y };
        }

        // 事件监听
        function handleMouseDown(e) {
            mouse.down = true;
            e.preventDefault();
            var coord = getMouseCoord(e);
            mouse.x = coord.x; mouse.y = coord.y;
        }
        function handleMouseUp(e) {
            mouse.down = false;
            mouse.clicks++;
            e.preventDefault();
            var coord = getMouseCoord(e);
            mouse.x = coord.x; mouse.y = coord.y;
        }
        function handleMouseMove(e) {
            var coord = getMouseCoord(e);
            mouse.x = coord.x; mouse.y = coord.y;
        }
        function handleTouchStart(e) {
            e.preventDefault();
            var touch = e.touches[0];
            mouse.down = true;
            var coord = getMouseCoord(touch);
            mouse.x = coord.x; mouse.y = coord.y;
        }
        function handleTouchEnd(e) {
            e.preventDefault();
            mouse.down = false;
            mouse.clicks++;
        }
        function handleTouchMove(e) {
            e.preventDefault();
            var touch = e.touches[0];
            var coord = getMouseCoord(touch);
            mouse.x = coord.x; mouse.y = coord.y;
        }

        canvas.addEventListener('mousedown', handleMouseDown);
        canvas.addEventListener('mouseup', handleMouseUp);
        canvas.addEventListener('mousemove', handleMouseMove);
        canvas.addEventListener('touchstart', handleTouchStart);
        canvas.addEventListener('touchend', handleTouchEnd);
        canvas.addEventListener('touchmove', handleTouchMove);

        // 渲染函数
        function render() {
            // 每帧强制同步尺寸（确保旋转后立即生效）
            syncCanvasSize();

            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.fillStyle = '#1a1a2e';
            ctx.fillRect(0, 0, canvas.width, canvas.height);

            // 网格（自适应步长）
            ctx.save();
            ctx.strokeStyle = '#2a2a4e';
            ctx.lineWidth = 1;
            var step = 40;
            for (var x = 0; x < canvas.width; x += step) {
                for (var y = 0; y < canvas.height; y += step) {
                    ctx.strokeRect(x, y, step, step);
                }
            }
            ctx.restore();

            // 橙色矩形
            ctx.fillStyle = 'rgb(255, 100, 50)';
            ctx.globalAlpha = 0.5;
            ctx.fillRect(10, 10, 100, 100);

            // 曲线 + 方形
            ctx.beginPath();
            ctx.moveTo(200, 200);
            ctx.lineTo(300, 200);
            ctx.quadraticCurveTo(300, 300, 200, 300);
            ctx.rect(700, 10, 90, 90);
            ctx.closePath();
            ctx.strokeStyle = 'white';
            ctx.lineWidth = 10.0;
            ctx.stroke();

            // 图片
            if (imgLoaded && !imgError) {
                try {
                    ctx.drawImage(img, 120, 10);
                    ctx.drawImage(img, 120, 74, 64, 32);
                } catch(e) {}
            } else if (imgError) {
                ctx.fillStyle = '#ff8888';
                ctx.font = '12px monospace';
                ctx.fillText("[logo.png 加载失败]", 120, 30);
            }

            // 鼠标光晕
            var r = mouse.down ? 60 : 30;
            var grad = ctx.createRadialGradient(mouse.x, mouse.y, 0, mouse.x, mouse.y, r);
            if (mouse.down) {
                grad.addColorStop(0, 'rgba(255,80,80,0.9)');
                grad.addColorStop(0.5, 'rgba(200,30,30,0.5)');
                grad.addColorStop(1, 'rgba(100,0,0,0)');
            } else {
                grad.addColorStop(0, 'rgba(80,180,255,0.9)');
                grad.addColorStop(0.5, 'rgba(40,120,200,0.4)');
                grad.addColorStop(1, 'rgba(0,40,80,0)');
            }
            ctx.fillStyle = grad;
            ctx.beginPath();
            ctx.arc(mouse.x, mouse.y, r, 0, Math.PI * 2);
            ctx.fill();

            // 文字信息
            ctx.fillStyle = '#ffffff';
            ctx.font = '13px monospace';
            ctx.fillText('✨ 全屏自适应 | 每帧同步尺寸', 10, 20);
            ctx.fillText('鼠标状态: ' + (mouse.down ? '按下 🔴' : '抬起 🔵'), 10, 38);
            ctx.fillText('点击次数: ' + mouse.clicks, 10, 54);
            ctx.fillText('坐标: (' + Math.round(mouse.x) + ', ' + Math.round(mouse.y) + ')', 10, 70);
            if (!imgLoaded && !imgError) {
                ctx.fillStyle = '#ffaa66';
                ctx.fillText('⏳ 正在加载 logo.png ...', 10, 88);
            } else if (imgError) {
                ctx.fillStyle = '#ff8866';
                ctx.fillText('⚠️ 图片加载失败，请确保 logo.png 存在', 10, 88);
            }

            drawFPS();
        }

        var fpsSmooth = 60, lastFrameTime = performance.now();
        function drawFPS() {
            var now = performance.now();
            var delta = Math.min(0.1, (now - lastFrameTime) / 1000);
            if (delta > 0) {
                var instantFPS = 1 / delta;
                fpsSmooth = fpsSmooth * 0.9 + instantFPS * 0.1;
            }
            lastFrameTime = now;
            ctx.font = '12px monospace';
            ctx.fillStyle = '#0f0';
            ctx.fillText('FPS: ' + Math.round(fpsSmooth), canvas.width - 70, 25);
        }

        // 启动动画循环
        function animationLoop() {
            render();
            requestAnimationFrame(animationLoop);
        }
        animationLoop();

        // 额外监听 resize 和 orientationchange（作为辅助）
        window.addEventListener('resize', function() {
            console.log("收到 resize 事件，主动同步");
            syncCanvasSize();
        });
        window.addEventListener('orientationchange', function() {
            console.log("收到 orientationchange 事件，主动同步");
            syncCanvasSize();
        });

        console.log("全屏自适应 Demo 已启动");
    }
})();