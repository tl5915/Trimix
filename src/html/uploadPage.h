#include <pgmspace.h>
#pragma once

const char uploadPage[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <title>Upload Icon</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body>
  <h1>Upload Icon</h1>
  <form id="iconForm" method="POST" action="/upload" enctype="multipart/form-data" onsubmit="return checkIcon();">
    <label for="iconInput">Choose a PNG (max 20KB):</label><br>
    <input type="file" name="upload" id="iconInput" accept=".png" required><br>
    <div id="iconStatus" class="info"></div>
    <input type="submit" value="Upload">
  </form>
  <p><a href="/">Return</a></p>

  <script>
    const iconInput = document.getElementById('iconInput');
    const iconStatus = document.getElementById('iconStatus');
    iconInput.addEventListener('change', () => {
      const file = iconInput.files[0];
      if (file) {
        const sizeKB = (file.size / 1024).toFixed(1);
        iconStatus.textContent = `Selected: ${file.name} (${sizeKB} KB)`;
      } else {
        iconStatus.textContent = '';
      }
    });
    function checkIcon() {
      const file = iconInput.files[0];
      if (!file) {
        alert('Please select a file.');
        return false;
      }
      const ext = file.name.split('.').pop().toLowerCase();
      if (!file.name.toLowerCase().endsWith('.png')) {
        alert('Invalid file type.');
        return false;
      }
      if (file.size > 20 * 1024) {
        alert('File size over 20 kB.');
        return false;
      }
      return true;
    }
  </script>
</body>
</html>
)HTML";