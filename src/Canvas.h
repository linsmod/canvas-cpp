(function() {
    var canvas = document.getElementById("c") || document.createElement('canvas');
    canvas.id = "c";
    canvas.width = 600;
    canvas.height = 200;
    canvas.style.display = 'block';
    document.body.appendChild(canvas);
    var ctx = canvas.getContext('2d');

    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, 600, 200);

    // 正三角形的几何参数
    var centerX = canvas.width / 2;
    var centerY = canvas.height - 60;   // 三角形中心（重心）
    var radius = 80;                     // 外接圆半径（顶点到中心的距离）

    for (let i = 0; i < 3; i++) {
        // 三个顶点的角度：0°, 120°, 240°（弧度制）
        let angle = i * Math.PI * 2 / 3;
        let x = centerX + radius * Math.cos(angle);
        let y = centerY + radius * Math.sin(angle);

        ctx.beginPath();            // 重要：避免路径累积
        ctx.arc(x, y, 6, 0, Math.PI * 2);
        ctx.fillStyle = `hsla(${Date.now() * 0.5 + i * 120}, 80%, 65%, 1)`;
        ctx.fill();
    }
})();