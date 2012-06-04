/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Note that the create wall wizard operates quite differently to the manage
 * wall feature.
 *
 * With the wizard, we create a new history entry each time you go backwards or
 * forwards. Hence, you can use the back/forwards buttons to navigate the
 * wizard. This matches the linear flow of the wizard.
 *
 * However, the URL does not change. It is always wall-maker/new. This is
 * because it doesn't make sense to provide a link to mid-way through a wizard.
 * We remember the current page using session storage so that if the user
 * refreshes during the wizard, we return to the correct page.
 *
 * The manage wall feature on the other hand does not create history entries but
 * DOES update the URL because it makes sense to be able to point to an
 * individual tab in the interface with a URL but you don't want to generate
 * millions of history entries every time you change tab. Restoring the correct
 * tab when you refresh the browser is managed by using document.location.hash.
 */

var CreateWallWizard =
{
  pages: null,

  init: function() {
    this.pages = document.querySelectorAll("#screen-new div.page");
    this.show(this.getIndex())
  },

  next: function() {
    var newIndex = this.getIndex() + 1;
    this.show(newIndex);
    history.pushState({ createWallPage: newIndex }, null, null);
  },

  prev: function() {
    var newIndex = this.getIndex()-1;
    if (newIndex < 0) {
      goToScreen("./");
    } else {
      this.show(newIndex);
      history.pushState({ createWallPage: newIndex }, null, null);
    }
  },

  cancel: function() {
    goToScreen("./");
    this.clearAll();
    this.show(0);
  },

  create: function() {
    // XXX Clear error message
    // Show loading screen
    this.next();
  },

  finish: function() {
    goToScreen("./");
    this.show(0);
  },

  show: function(index) {
    // Clamp index to valid range
    index = index <= 0
          ? 0
          : index >= this.pages.length ? this.pages.length - 1 : index;
    // Update (stored) index
    this.setIndex(index);

    // Display the appropriate page
    for (var i = 0; i < this.pages.length; i++) {
      if (i === index) {
        this.pages[i].style.display = "block";
      } else {
        this.pages[i].style.display = "none";
      }
    }

    // Toggle which buttons are active.
    //
    // Typically, the arrangement of buttons is as follows:
    //  -- first page      : cancel, next
    //  -- ...             : cancel, prev, next
    //  -- final page      : cancel, prev, create
    //  -- waiting page    : (none)
    //  -- completion page : finish
    var buttonBar    = document.getElementsByClassName("wizardButtons")[0];
    var prevButton   = buttonBar.getElementsByClassName("prevButton")[0];
    var nextButton   = buttonBar.getElementsByClassName("nextButton")[0];
    var createButton = buttonBar.getElementsByClassName("createButton")[0];
    var page = this.pages[index];
    // Hide buttons for those pages that don't want them
    buttonBar.style.display = page.classList.contains('nobutton')
                            ? 'none' : 'block';
    // Hide prev button on first page
    prevButton.style.visibility = index == 0 ? 'hidden' : 'visible';
    // Toggle next vs create button
    var finalPage = page.classList.contains('final');
    nextButton.style.display   = finalPage ? 'none'   : 'inline';
    createButton.style.display = finalPage ? 'inline' : 'none';

    // XXX Check validation state of current page and disable next / finish
    //     button as necessary---initial state??
  },

  getIndex: function() {
    // XXX Should this clamp the number??
    return sessionStorage.getItem("createWallPage") !== null
           ? parseInt(sessionStorage.getItem("createWallPage"))
           : 0
  },

  setIndex: function(index) {
    sessionStorage.setItem("createWallPage", index);
  },

  createError: function() {
    // XXX Set error message
    this.prev();
  },

  createSuccess: function() {
    // XXX Get ID and update finish page links
    this.clearAll();
    this.next();
  },

  clearAll: function() {
    // XXX Iterate over all input elements
    // XXX Clear sessionStorage etc.
  },

  popHistory: function(evt) {
    if (typeof evt.state === "object" && evt.state.createWallPage) {
      var index = evt.state.createWallPage;
      this.show(index);
    } else if (this.getIndex() !== 0) {
      this.show(0);
    }
  },
}

function initCreateWallWizard() {
  window.addEventListener('popstate',
    CreateWallWizard.popHistory.bind(CreateWallWizard), false);
  CreateWallWizard.init();
}
window.addEventListener('load', initCreateWallWizard, false);

// XXX Register listener to all form changes
//  -- validates current page and updates disabled/enabled state of next
//     / create button
