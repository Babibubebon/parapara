/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

var EditorUI = EditorUI || {};

EditorUI.INITIAL_SPEED_FPS = 3.3;
EditorUI.UPLOAD_PATH       = "../api/upload_anim.php";
EditorUI.SEND_EMAIL_PATH   = "../api/email_anim.php";

EditorUI.init = function() {
  var paraparaRoot = document.getElementById("parapara");
  ParaPara.init(paraparaRoot);
  EditorUI.initControls();
  // Disabling full-screen mode for now since:
  // a) there's no UI for it for tablets
  // b) it prevents our overlays from appearing
  /*
  var elem = document.getElementById("container");
  if (elem.requestFullScreen) {
    elem.requestFullScreen();
  } else if (elem.mozRequestFullScreen) {
    elem.mozRequestFullScreen();
  } else if (elem.webkitRequestFullScreen) {
    elem.webkitRequestFullScreen();
  }
  */
  EditorUI.updateLayout();
}
window.addEventListener("load", EditorUI.init, false);

EditorUI.initControls = function() {
  // This method gets called on init and ALSO on reset. Therefore, we must
  // assume it will be called multiple times. When adding event listeners we
  // must be sure to pass exactly the same function object so that
  // addEventListener can detect the duplicate and filter it out. If we use
  // function objects generated on the fly we'll end up accumulating event
  // listeners and, at best, getting slower and slower.
  // XXX
  EditorUI.initColors();
  EditorUI.initWidths();
  EditorUI.initTools();
  // EditorUI.initFrameControls();
  EditorUI.initNavControls();
  EditorUI.initSpeedMeter();

  // Add a catch-all handler to call preventDefault on mouse events.
  // This is necessary for disabling the chrome that flies in from offscreen
  // when you drag in old versions of the Fennec tablet UI (i.e. Firefox 9-ish)
  var container = document.getElementById("container");
  container.addEventListener("mousemove", EditorUI.catchAll, false);
  container.addEventListener("touchmove", EditorUI.catchAll, false);
}

EditorUI.catchAll = function(e) {
  e.preventDefault();
}

// -------------- Navigation -----------

EditorUI.prevFrame = function() {
  var result = ParaPara.prevFrame();
  EditorUI.updateFrameDisplay(result.index+1, result.count);
}

EditorUI.nextFrame = function() {
  var result = ParaPara.nextFrame();
  if (result.added)
    EditorUI.changeTool("pencil");
  EditorUI.updateFrameDisplay(result.index+1, result.count);
}

EditorUI.deleteFrame = function() {
  var result = ParaPara.deleteFrame();
  EditorUI.updateFrameDisplay(result.index+1, result.count);
}

EditorUI.animate = function() {
  document.getElementById("play").contentDocument.hide();
  EditorUI.meter.enable();
  var speed = EditorUI.meter.getValue();
  ParaPara.animate(speed);
}

EditorUI.returnToEditing = function() {
  ParaPara.removeAnimation();
  var playButton = document.getElementById("play");
  playButton.style.display = 'block';
  playButton.contentDocument.show();
  EditorUI.meter.disable();
}

EditorUI.reset = function() {
  document.forms[0].reset();
  ParaPara.reset();
  EditorUI.initControls();
}

// -------------- Sending -----------

EditorUI.send = function() {
  EditorUI.displayNote("noteSending");
  var metadata = {};
  metadata.title  = document.forms[0].title.value.trim();
  metadata.author = document.forms[0].name.value.trim();
  ParaPara.send(EditorUI.UPLOAD_PATH, EditorUI.sendSuccess, EditorUI.sendFail,
                metadata);
}

EditorUI.getRadioValue = function(radio) {
  for (var i = 0; i < radio.length; ++i) {
    if (radio[i].checked)
      return radio[i].value;
  }
  return undefined;
}

EditorUI.sendSuccess = function(response) {
  EditorUI.displayNote("noteSendingComplete");
  if (response.url) {
    // If we have a URL, prepare the sharing screen to be shown after the
    // success screen
    var parts = [];
    if (response.qrcode) {
      parts.push("<img src=\"" + response.qrcode + "\" class=\"qrcode\">");
    } else {
      var qr = new QRCode(0, QRCode.QRErrorCorrectLevel.M);
      qr.addData(response.url);
      qr.make();
      var image = qr.getImage(4 /*cell size*/);
      parts.push("<img src=\"" + image.data +
                 "\" width=\"" + image.width +
                 "\" height\"" + image.height +
                 "\" alt=\"" + response.url + "\">");
    }
    // We deliberately DON'T wrap the URL in an <a> element since we don't
    // really want users following the link and navigating away from the editor.
    // It's just there so they can copy it down into a notepad as a last resort.
    parts.push("<div class=\"url\">" + response.url + "</div>");
    parts.push("<input type=\"hidden\" name=\"animation-id\" value=\"" +
      response.id + "\">");
    var linkBlock = document.getElementById("animation-link");
    linkBlock.innerHTML = parts.join("");
    EditorUI.clearEmailForm();
    // Sharing screen is ready, queue it to display after the success note has
    // ended
    EditorUI.fadeNote(
      function() { EditorUI.displayNote("noteShare"); }
    );
    // EditorUI.reset() will be called when the sharing screen is dismissed
  } else {
    // No URL, just show success message
    EditorUI.fadeNote();
    EditorUI.reset();
  }
}

EditorUI.sendFail = function(code) {
  switch (code) {
    case ParaPara.SEND_ERROR_NO_ANIMATION:
      EditorUI.displayNote("noteNoAnimation");
      console.debug("No animation to send");
      break;

    case ParaPara.SEND_ERROR_TIMEOUT:
      EditorUI.displayNote("noteSendingFailed");
      console.debug("Timed out sending animation");
      break;

    case ParaPara.SEND_ERROR_FAILED_SEND:
      EditorUI.displayNote("noteSendingFailedFatal");
      console.debug("Failed to send animation");
      break;

    case ParaPara.SEND_ERROR_NO_ACCESS:
      EditorUI.displayNote("noteSendingFailed");
      console.debug("No access to remote server");
      break;

    case ParaPara.SEND_ERROR_SERVER_ERROR:
      EditorUI.displayNote("noteSendingFailed");
      console.debug("Server error");
      break;

    case ParaPara.SEND_ERROR_SERVER_NOT_LIVE:
      EditorUI.displayNote("noteNotLive");
      console.debug("Server not live");
      break;

    default:
      EditorUI.displayNote("noteSendingFailed");
      console.debug("Unknown error");
      break;
  }
}

EditorUI.clearNoteAndReset = function() {
  EditorUI.clearNote();
  EditorUI.reset();
}

// -------------- Error messages -----------

EditorUI.displayNote = function(id) {
  var notes = document.getElementsByClassName("overlay-contents");
  for (var i = 0; i < notes.length; ++i) {
    var note = notes[i];
    note.style.display = note.id == id ? "block" : "none";
  }
  var overlay = document.getElementById("overlay");
  overlay.style.display = "";
}

EditorUI.clearNote = function() {
  var overlay = document.getElementById("overlay");
  overlay.style.display = "none";
}

EditorUI.fadeNote = function(callback) {
  var notes = document.getElementsByClassName("overlay-contents");
  var currentNote = null;
  for (var i = 0; i < notes.length; ++i) {
    var note = notes[i];
    if (note.style.display !== "none") {
      currentNote = note;
      break;
    }
  }
  if (!currentNote)
    return;
  currentNote.classList.add("fadeOut");
  var onend = callback
    ? function(evt) { EditorUI.finishFade(evt); callback(); }
    : EditorUI.finishFade;
  currentNote.addEventListener("animationend", onend, false);
}

EditorUI.finishFade = function(evt) {
  evt.target.classList.remove("fadeOut");
  EditorUI.clearNote();
}

// -------------- Sending email -----------

EditorUI.clearEmailForm = function() {
  var form = document.forms["email-form"];
  form.reset();
  form.email.classList.add("placeholder");
  EditorUI.toggleEmailPlaceholder();
  EditorUI.clearEmailStatus();
}

EditorUI.toggleEmailPlaceholder = function() {
  var emailField = document.forms["email-form"].email;
  if (document.activeElement == emailField) {
    if (emailField.classList.contains("placeholder")) {
      emailField.value = "";
      emailField.classList.remove("placeholder");
    }
  } else if (!emailField.value) {
    emailField.classList.add("placeholder");
    emailField.value = "例：parapara@yahoo.co.jp";
    emailField.validity.valid = true;
  }
}

EditorUI.sendEmail = function() {
  EditorUI.clearEmailStatus();

  // If no email, ignore
  var emailField = document.forms["email-form"].email;
  var email = emailField.value.trim();
  if (!email || emailField.classList.contains("placeholder"))
    return;

  // Email address validation: For UAs that support HTML5 form validation, we
  // won't get this far if the address isn't valid. For other UAs, we'll just
  // rely on the server to do the validation.

  // Get ID for animation (stored in a hidden field)
  // (We send the ID rather than the animation URL so that people don't
  // commandeer the server to send arbitrary URLs)
  var animationId =
    parseInt(document.forms["email-form"].elements["animation-id"].value);
  if (!animationId) {
    EditorUI.setEmailStatus("failed");
    return;
  }

  // Disable submit button so we don't get double-submits from those who like to
  // double-click everything
  document.getElementById("email-button").disabled = true;

  // Send away
  EditorUI.setEmailStatus("waiting");
  ParaPara.sendEmail(email, animationId, EditorUI.SEND_EMAIL_PATH,
                     EditorUI.sendEmailSuccess, EditorUI.sendEmailFail);
}

EditorUI.clearEmailStatus = function() {
  EditorUI.setEmailStatus("");
}

EditorUI.setEmailStatus = function(statusClass) {
  var progressIcon = document.getElementById("email-progress");
  progressIcon.classList.remove("waiting");
  progressIcon.classList.remove("failed");
  progressIcon.classList.remove("ok");
  if (statusClass) {
    progressIcon.classList.add(statusClass);
  }
}

EditorUI.sendEmailSuccess = function() {
  // Clear email field for sending another email
  EditorUI.clearEmailForm();
  document.getElementById("email-button").disabled = false;

  // Update status
  EditorUI.setEmailStatus("ok");
}

EditorUI.sendEmailFail = function() {
  // Update status
  EditorUI.setEmailStatus("failed");
  document.getElementById("email-button").disabled = false;
}

// -------------- Colors -----------

EditorUI.initColors = function() {
  var picker = document.getElementById("picker");
  EditorUI.setColor(picker.contentDocument.getRandomColor());
  picker.contentDocument.addEventListener("colorchange", EditorUI.onChangeColor,
                                          false);
}

EditorUI.onChangeColor = function(evt) {
  var color = evt.detail.color;
  EditorUI.setColor(color);
  EditorUI.changeTool("pencil");
}

EditorUI.setColor = function(color) {
  ParaPara.currentStyle.currentColor = color;
  EditorUI.updateBrushPreviewColor(color);
}

EditorUI.updateBrushPreviewColor = function(color) {
  var widths = document.getElementById("widths");
  widths.contentDocument.setColor(color);
}

// -------------- Widths -----------

// Width values
EditorUI.widthTable = new Array();
EditorUI.widthTable[0] = 4;
EditorUI.widthTable[1] = 8;
EditorUI.widthTable[2] = 12;

EditorUI.initWidths = function() {
  var widths = document.getElementById("widths");
  ParaPara.currentStyle.strokeWidth = EditorUI.widthTable[1];
  ParaPara.currentStyle.eraseWidth = EditorUI.widthTable[1];
  widths.contentDocument.setWidth(1);
  widths.contentDocument.addEventListener("widthchange", EditorUI.onWidthChange,
                                          false);
}

EditorUI.onWidthChange = function(evt) {
  var width = evt.detail.width;
  console.assert(width >= 0 && width < EditorUI.widthTable.length,
                 "Out of range width value");
  var widthValue = EditorUI.widthTable[width];

  // Apply change
  if (ParaPara.getMode() === "draw")
    ParaPara.currentStyle.strokeWidth = widthValue;
  else
    ParaPara.currentStyle.eraseWidth = widthValue;
}

// -------------- Tools -----------

EditorUI.initTools = function() {
  var picker = document.getElementById("picker");
  picker.contentDocument.addEventListener("eraserselect", EditorUI.selectEraser,
                                          false);
  EditorUI.changeTool("pencil");
}

EditorUI.selectEraser = function() {
  EditorUI.changeTool("eraser");
}

EditorUI.showEraser = function() {
  var widths = document.getElementById("widths");
  widths.contentDocument.setEraserMode();
}

// tool = "pencil" | "eraser"
EditorUI.changeTool = function(tool) {
  var changed = false;
  switch (tool) {
    case "pencil":
      changed = ParaPara.setDrawMode();
      break;
    case "eraser":
      changed = ParaPara.setEraseMode();
      break;
  }
  if (!changed)
    return;

  // Update width picker
  switch (tool) {
    case "pencil":
      EditorUI.updateBrushPreviewColor(ParaPara.currentStyle.currentColor);
      break;
    case "eraser":
      EditorUI.showEraser();
      break;
  }
}

// -------------- Frame controls -----------

EditorUI.initFrameControls = function() {
  var frameControls = document.getElementById("frameControls");
  var prev = frameControls.contentDocument.getElementById("prev");
  prev.addEventListener("click", EditorUI.prevFrame, false);
  var next = frameControls.contentDocument.getElementById("next");
  next.addEventListener("click", EditorUI.nextFrame, false);
  EditorUI.updateFrameDisplay(1, 1);
}

EditorUI.updateFrameDisplay = function(currentFrame, numFrames) {
  var frameControls = document.getElementById("frameControls");
  var numerator   = frameControls.contentDocument.getElementById("numerator");
  var denominator = frameControls.contentDocument.getElementById("denominator");
  numerator.textContent   = currentFrame;
  denominator.textContent = numFrames;
}

// -------------- Init nav controls -----------

EditorUI.initNavControls = function() {
  var animate = document.getElementById("play");
  animate.contentDocument.addEventListener("click", EditorUI.animate, false);
  animate.contentDocument.addEventListener("hidden", EditorUI.hidePlayButton,
                                           false);

  // On iOS Safari we don't see to get click events when we attach it to the
  // play button (probably due to the way it is absolutely-positioned). So we
  // attach the event handler to the outer group instead.
  var playContainer = document.getElementById("play-container");
  playContainer.addEventListener("click", EditorUI.animate, false);

  animate.contentDocument.show();
  var send = document.getElementById("end");
  send.contentDocument.addEventListener("click", EditorUI.send, false);
}

EditorUI.hidePlayButton = function() {
  // Unless we actually make the <object> itself display:none the events don't
  // seem to fall through to the meter. I tried setting pointer-events:none
  // inside the SVG but it doesn't seem to work.
  document.getElementById("play").style.display = 'none';
}

EditorUI.confirmClear = function() {
  EditorUI.displayNote("noteConfirmDelete");
}

// -------------- Speed control -----------

EditorUI.initSpeedMeter = function() {
  var meterObject = document.getElementById("meter");
  if (!EditorUI.meter) {
    EditorUI.meter =
      new Meter(0.65, 12.5, 0.2, meterObject,EditorUI.changeSpeed);
  }
  EditorUI.meter.setValue(EditorUI.INITIAL_SPEED_FPS);
  EditorUI.meter.disable();
}

EditorUI.changeSpeed = function(sliderValue) {
  if (!ParaPara.animator)
    return;
  ParaPara.animator.setSpeed(sliderValue);
}

// -------------- UI layout -----------

EditorUI.updateLayout = function() {
  // XXX All this needs to be redone
  var controlsHeight = controlsWidth = 0;
  var controls = document.getElementsByClassName("controlPanel");
  for (var i = 0; i < controls.length; i++) {
    if (controls[i].classList.contains('vertical')) {
      controlsWidth += controls[i].offsetWidth;
    } else {
      controlsHeight += controls[i].offsetHeight;
    }
  }
  var availHeight = window.innerHeight - controlsHeight;
  var availWidth  = window.innerWidth - controlsWidth;
  var vbHeight = 300;
  var vbWidth = vbHeight * availWidth / availHeight;

  // Set the SVG canvas size explicitly.
  var canvas = document.getElementById("canvas");
  canvas.style.setProperty("width", availWidth + "px", "");
  canvas.style.setProperty("height", availHeight + "px", "");
  canvas.setAttribute("viewBox", [0, 0, vbWidth, vbHeight].join(" "));

  // Workaround Safari bugs regarding resizing SVG by setting the height of
  // referenced SVG files explicitly
  var contents = document.querySelectorAll(".panelContents");
  for (var i = 0; i < contents.length; i++) {
    var specifiedRatio =
      parseFloat(contents[i].getAttribute('data-aspect-ratio'));
    if (!specifiedRatio)
       continue;
    var style = window.getComputedStyle(contents[i]);
    var actualRatio = parseInt(style.width) / parseInt(style.height);
    // If the actual ratio differs from the specified ratio by more than 5%
    // update the height
    var error = Math.abs(specifiedRatio - actualRatio) / specifiedRatio;
    if (Math.abs(specifiedRatio - actualRatio) / specifiedRatio >= 0.05) {
      var adjustedHeight = parseInt(style.width) / specifiedRatio;
      contents[i].setAttribute("height", adjustedHeight);
    }
  }

  // Manually perform calc() behavior for browsers that don't support it
  var borders = document.querySelectorAll(".inner-border");
  for (var i = 0; i < borders.length; i++) {
    var border = borders[i];
    var actualHeight = parseInt(window.getComputedStyle(border).height);
    // height: calc(100% - 14px);
    var expectedHeight = window.innerHeight - 14;
    if (actualHeight != expectedHeight) {
      border.style.height = expectedHeight + 'px';
    }
  }
}
window.addEventListener("resize", EditorUI.updateLayout, false);
window.addEventListener("orientationchange", EditorUI.updateLayout, false);
