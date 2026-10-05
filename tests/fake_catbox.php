<?php
// Stand-in for https://catbox.moe/user/api.php (run by tests/fake_web.py with "php -S"): PHP parses the upload
// exactly like the real service. A good upload gets a file URL; anything else gets an empty answer - which is
// what catbox does (the plugin logged "catbox upload failed (HTTP 200): no answer").
if (($_POST['reqtype'] ?? '') === 'fileupload' && isset($_FILES['fileToUpload']) && $_FILES['fileToUpload']['error'] === 0
    && $_FILES['fileToUpload']['size'] > 0)
    echo 'https://files.catbox.moe/t3st42.' . pathinfo($_FILES['fileToUpload']['name'], PATHINFO_EXTENSION);
