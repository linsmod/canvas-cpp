(function(global) {
  'use strict';

  // Use globalThis as the actual global scope (QuickJS compatibility)
  var W = (typeof globalThis !== 'undefined') ? globalThis : global;

  // ============================================================
  // Navigator
  // ============================================================
  W.navigator = {
    userAgent: 'Canvas++/1.0',
    clipboard: {
      writeText: function(text) {
        return Promise.resolve();
      },
      readText: function() {
        return Promise.resolve('');
      }
    },
    storage: window._opfsNativeStorage
  };

  // ============================================================
  // Performance
  // ============================================================
  W.performance = {
    now: function() { return Date.now(); },
    memory: { usedJSHeapSize: 0, totalJSHeapSize: 0, jsHeapSizeLimit: 0 }
  };

  W.matchMedia = function(query) {
    return { matches: false };
  };

  // ============================================================
  // Style proxy - native-backed CSSStyleDeclaration
  // ============================================================
  function createStyleProxy() {
    if (W.__createStyle) {
      return W.__createStyle();
    }
    // Fallback to plain object if native not available
    return {};
  }

  // ============================================================
  // Shared element tree helpers
  // ============================================================

  function _removeFromOldParent(child) {
    if (child._parent) {
      var idx = child._parent._children.indexOf(child);
      if (idx >= 0) child._parent._children.splice(idx, 1);
    }
  }

  function _updateSiblings(parent) {
    var children = parent._children;
    for (var i = 0; i < children.length; i++) {
      children[i]._previousSibling = (i > 0) ? children[i - 1] : null;
      children[i]._nextSibling = (i < children.length - 1) ? children[i + 1] : null;
    }
  }

  function _setParent(child, parent) {
    child._parent = parent;
    child.isConnected = !!parent;
  }

  // ============================================================
  // elementFromPoint helpers
  // ============================================================

  function _elementFromPointImpl(root, x, y) {
    if (!root || typeof root.getBoundingClientRect !== 'function') return null;
    // Walk children in reverse order (last = visually topmost / highest z-order)
    var children = root._children || [];
    for (var i = children.length - 1; i >= 0; i--) {
      var result = _elementFromPointImpl(children[i], x, y);
      if (result) return result;
    }
    // Check if point is inside this element
    var rect = root.getBoundingClientRect();
    if (rect && x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) {
      return root;
    }
    return null;
  }

  // ============================================================
  // Element (base for all DOM-like nodes)
  // ============================================================
  function ElementBase() {
    this.style = createStyleProxy();
    this._children = [];
    this._events = {};
  }

  ElementBase.prototype.appendChild = function(child) {
    if (!child) return child;
    _removeFromOldParent(child);
    this._children.push(child);
    _setParent(child, this);
    _updateSiblings(this);
    return child;
  };

  ElementBase.prototype.insertBefore = function(child, ref) {
    if (!child) return child;
    if (!ref) return this.appendChild(child);
    var idx = this._children.indexOf(ref);
    if (idx < 0) return child;
    _removeFromOldParent(child);
    this._children.splice(idx, 0, child);
    _setParent(child, this);
    _updateSiblings(this);
    return child;
  };

  ElementBase.prototype.replaceChild = function(newChild, oldChild) {
    if (!newChild || !oldChild) return oldChild;
    var idx = this._children.indexOf(oldChild);
    if (idx < 0) return oldChild;
    _removeFromOldParent(newChild);
    this._children[idx] = newChild;
    _setParent(newChild, this);
    oldChild._parent = null;
    oldChild.isConnected = false;
    _updateSiblings(this);
    return oldChild;
  };

  ElementBase.prototype.removeChild = function(child) {
    var idx = this._children.indexOf(child);
    if (idx >= 0) this._children.splice(idx, 1);
    _setParent(child, null);
    _updateSiblings(this);
    return child;
  };

  ElementBase.prototype.remove = function() {
    if (this._parent) {
      this._parent.removeChild(this);
    }
  };

  ElementBase.prototype.contains = function(other) {
    if (!other) return false;
    if (other === this) return true;
    for (var i = 0; i < this._children.length; i++) {
      if (this._children[i].contains(other)) return true;
    }
    return false;
  };

  ElementBase.prototype.getBoundingClientRect = function() {
    var x = this.x || this.left || 0;
    var y = this.y || this.top || 0;
    var w = this.width || this.clientWidth || this.offsetWidth || 0;
    var h = this.height || this.clientHeight || this.offsetHeight || 0;
    return { x: x, y: y, width: w, height: h, top: y, left: x, right: x + w, bottom: y + h };
  };

  ElementBase.prototype.click = function() {};
  ElementBase.prototype.focus = function() {};
  ElementBase.prototype.setAttribute = function(name, value) { this[name] = value; };
  ElementBase.prototype.getAttribute = function(name) {
    return this.hasOwnProperty(name) ? this[name] : null;
  };
  ElementBase.prototype.removeAttribute = function(name) { delete this[name]; };
  ElementBase.prototype.addEventListener = W.addEventListener;
  ElementBase.prototype.removeEventListener = W.removeEventListener;

  Object.defineProperty(ElementBase.prototype, 'parentNode', {
    get: function() { return this._parent; }
  });
  Object.defineProperty(ElementBase.prototype, 'parentElement', {
    get: function() { return this._parent; }
  });
  Object.defineProperty(ElementBase.prototype, 'firstChild', {
    get: function() { return this._children.length > 0 ? this._children[0] : null; }
  });
  Object.defineProperty(ElementBase.prototype, 'lastChild', {
    get: function() { return this._children.length > 0 ? this._children[this._children.length - 1] : null; }
  });
  Object.defineProperty(ElementBase.prototype, 'nextSibling', {
    get: function() { return this._nextSibling || null; }
  });
  Object.defineProperty(ElementBase.prototype, 'previousSibling', {
    get: function() { return this._previousSibling || null; }
  });
  Object.defineProperty(ElementBase.prototype, 'children', {
    get: function() { return this._children.slice(); }
  });
  Object.defineProperty(ElementBase.prototype, 'childElementCount', {
    get: function() { return this._children.length; }
  });

  // ============================================================
  // FakeElement
  // ============================================================
  function FakeElement() {
    ElementBase.call(this);
  }
  FakeElement.prototype = Object.create(ElementBase.prototype);
  FakeElement.prototype.constructor = FakeElement;

  // ============================================================
  // Canvas element proxy
  // ============================================================
  function CanvasElement() {
    ElementBase.call(this);
    this._parent = null;
    this._width = 300;
    this._height = 200;
    this.tabIndex = 0;
  }
  CanvasElement.prototype = Object.create(ElementBase.prototype);
  CanvasElement.prototype.constructor = CanvasElement;

  CanvasElement.prototype.getContext = function(type) {
    if (W.getContext) {
      if (W.__setCanvasElementRef) {
        W.__setCanvasElementRef(this);
      }
      return W.getContext(type);
    }
    return null;
  };

  CanvasElement.prototype.getBoundingClientRect = function() {
    var w = this._width || W.width || 800;
    var h = this._height || W.height || 600;
    return { x: 0, y: 0, width: w, height: h, top: 0, left: 0, right: w, bottom: h };
  };

  Object.defineProperty(CanvasElement.prototype, 'width', {
    get: function() { return this._width !== undefined ? this._width : (W.width || 800); },
    set: function(val) {
      this._width = val;
      if (W.__resizeCanvas) W.__resizeCanvas(this, this._width, this._height);
    }
  });

  Object.defineProperty(CanvasElement.prototype, 'height', {
    get: function() { return this._height !== undefined ? this._height : (W.height || 600); },
    set: function(val) {
      this._height = val;
      if (W.__resizeCanvas) W.__resizeCanvas(this, this._width, this._height);
    }
  });

  Object.defineProperty(CanvasElement.prototype, 'clientWidth', {
    get: function() { return this._width !== undefined ? this._width : (W.width || 800); }
  });

  Object.defineProperty(CanvasElement.prototype, 'clientHeight', {
    get: function() { return this._height !== undefined ? this._height : (W.height || 600); }
  });

  CanvasElement.prototype.toDataURL = function(type) {
    if (W.__canvasToDataURL) {
      return W.__canvasToDataURL(type || 'image/png');
    }
    return 'data:image/png,';
  };

  // ============================================================
  // document
  // ============================================================

  var _elements = [];
  var _docBody = new FakeElement();
  var _docHead = new FakeElement();
  var _docElement = new FakeElement();

  _docBody.tagName = 'body';
  _docHead.tagName = 'head';
  _docElement.tagName = 'html';

  // Wire up document tree: document -> html -> body/head
  _docElement.appendChild(_docHead);
  _docElement.appendChild(_docBody);

  W.document = {
    _events: {},
    _children: [_docElement],
    style: createStyleProxy(),

    getElementById: function(id) {
      return _elements.find(function(x) {
        return x && (x.id == id || x.getAttribute('id') == id);
      }) || null;
    },

    createElement: function(tag) {
        var el = (tag === 'canvas') ? new CanvasElement() : new FakeElement();
        el.tagName = tag;
        _elements.push(el);
        return el;
    },

    addEventListener: W.addEventListener,
    removeEventListener: W.removeEventListener,

    body: _docBody,
    head: _docHead,
    documentElement: _docElement,

    querySelector: function(sel) {
      if (typeof sel !== 'string') return null;
      sel = sel.trim();
      if (sel.charAt(0) === '#') {
        return this.getElementById(sel.substring(1));
      }
      if (sel === 'canvas') {
        return this.getElementById('game-canvas');
      }
      return null;
    },

    querySelectorAll: function(sel) {
      return [];
    },

    createDocumentFragment: function() {
      var frag = new FakeElement();
      frag.tagName = 'document-fragment';
      return frag;
    },

    elementFromPoint: function(x, y) {
      return _elementFromPointImpl(this._children[0], x, y) || null;
    }
  };

  // Link html -> document for correct event bubbling
  _setParent(_docElement, W.document);

  // document tree properties
  Object.defineProperty(W.document, 'parentNode', {
    get: function() { return W.window || null; }
  });
  Object.defineProperty(W.document, 'parentElement', {
    get: function() { return null; }
  });
  Object.defineProperty(W.document, 'firstChild', {
    get: function() { return _docElement; }
  });
  Object.defineProperty(W.document, 'lastChild', {
    get: function() { return _docElement; }
  });
  Object.defineProperty(W.document, 'childElementCount', {
    get: function() { return 1; }
  });

  // Shared backing store for hidden / visibilityState
  (function(doc) {
    var _hidden = false;
    Object.defineProperty(doc, 'hidden', {
      get: function() { return _hidden; },
      set: function(val) { _hidden = !!val; },
      enumerable: true,
      configurable: true
    });
    Object.defineProperty(doc, 'visibilityState', {
      get: function() { return _hidden ? 'hidden' : 'visible'; },
      set: function(val) { _hidden = (val === 'hidden'); },
      enumerable: true,
      configurable: true
    });
  })(W.document);

  // ============================================================
  // localStorage polyfill (in-memory)
  // ============================================================
  var _localStorage = {};
  W.localStorage = {
    getItem: function(key) {
      return _localStorage.hasOwnProperty(key) ? _localStorage[key] : null;
    },
    setItem: function(key, val) {
      _localStorage[key] = String(val);
    },
    removeItem: function(key) {
      delete _localStorage[key];
    },
    clear: function() {
      _localStorage = {};
    }
  };

  // ============================================================
  // console fallback (in case native one is missing features)
  // ============================================================
  if (!W.console) {
    W.console = {
      log: function() { /* no-op or native */ },
      warn: function() {},
      error: function() {},
      info: function() {},
      debug: function() {}
    };
  } else {
    // Ensure warn/error exist even if native console only has log
    if (typeof W.console.warn === 'undefined') W.console.warn = W.console.log;
    if (typeof W.console.error === 'undefined') W.console.error = W.console.log;
    if (typeof W.console.info === 'undefined') W.console.info = W.console.log;
    if (typeof W.console.debug === 'undefined') W.console.debug = W.console.log;
  }

  // ============================================================
  // URL
  // ============================================================
  var _blobUrlCounter = 0;
  var _blobUrls = {};
  W.URL = {
    createObjectURL: function(obj) {
      var url = 'blob:nodedata/' + (++_blobUrlCounter);
      _blobUrls[url] = obj;
      return url;
    },
    revokeObjectURL: function(url) {
      delete _blobUrls[url];
    },
    getBlob: function(url) {
      return _blobUrls[url];
    }
  };

  // ============================================================
  // Blob
  // ============================================================
  W.Blob = function(parts, opts) {
    this._parts = parts || [];
    this._type = (opts && opts.type) || '';
    this.size = 0;
    this.type = this._type;
    // Concatenate all parts
    var self = this;
    for (var i = 0; i < this._parts.length; i++) {
      var part = this._parts[i];
      if (typeof part === 'string') {
        self.size += part.length;
      }
    }
  };

  // ============================================================
  // FileReader
  // ============================================================
  W.FileReader = function() {
    this.result = null;
    this.onload = null;
    this.onerror = null;
    this.readyState = 0;
  };
  W.FileReader.prototype.readAsText = function(blob) {
    this.readyState = 1; // LOADING
    var self = this;
    // Simulate async
    if (W.setTimeout) {
      W.setTimeout(function() {
        try {
          var content = '';
          if (blob && blob._parts) {
            for (var i = 0; i < blob._parts.length; i++) {
              var part = blob._parts[i];
              if (typeof part === 'string') {
                content += part;
              }
            }
          }
          self.result = content;
          self.readyState = 2; // DONE
          if (self.onload) {
            self.onload({ target: self });
          }
        } catch (e) {
          self.readyState = 2;
          if (self.onerror) {
            self.onerror(e);
          }
        }
      }, 0);
    }
  };
  W.FileReader.prototype.readAsArrayBuffer = function(blob) {
    this.readyState = 1;
    var self = this;
    if (W.setTimeout) {
      W.setTimeout(function() {
        self.readyState = 2;
        self.result = new ArrayBuffer(0);
        if (self.onload) self.onload({ target: self });
      }, 0);
    }
  };

  // ============================================================
  // fetch() polyfill - uses native __fetch for file reading + MIME detection
  // ============================================================
  W.fetch = function(url, options) {
    return new Promise(function(resolve, reject) {
      if (!W.__fetch) {
        resolve({
          ok: false, status: 404, statusText: 'Not Found (no __fetch)',
          url: url,
          text: function() { return Promise.resolve(''); },
          json: function() { return Promise.reject(new Error('Not Found')); },
          blob: function() { return Promise.reject(new Error('Not Found')); },
          arrayBuffer: function() { return Promise.reject(new Error('Not Found')); },
          headers: { get: function() { return null; }, has: function() { return false; } }
        });
        return;
      }

      try {
        var result = W.__fetch(url);
        if (!result) {
          reject(new Error('fetch returned null'));
          return;
        }

        var ok = result.ok;
        var status = result.status;
        var statusText = result.statusText;
        var respUrl = result.url;
        var contentType = result.contentType || 'text/plain';
        var body = result.body;

        var response = {
          ok: ok,
          status: status,
          statusText: statusText,
          url: respUrl,
          type: 'basic',
          redirected: false,
          headers: {
            get: function(name) {
              if (name.toLowerCase() === 'content-type') return contentType;
              return null;
            },
            has: function(name) {
              return name.toLowerCase() === 'content-type';
            },
            forEach: function(cb) {
              cb(contentType, 'content-type');
            },
            entries: function() {
              return [['content-type', contentType]][Symbol.iterator]();
            },
            keys: function() {
              return ['content-type'][Symbol.iterator]();
            },
            values: function() {
              return [contentType][Symbol.iterator]();
            }
          },
          text: function() { return Promise.resolve(body !== null ? String(body) : ''); },
          json: function() {
            if (body === null) return Promise.reject(new Error('No body'));
            try { return Promise.resolve(JSON.parse(String(body))); }
            catch (e) { return Promise.reject(e); }
          },
          blob: function() {
            if (body === null) return Promise.reject(new Error('No body'));
            return Promise.resolve(new W.Blob([String(body)], { type: contentType }));
          },
          arrayBuffer: function() {
            if (body === null) return Promise.reject(new Error('No body'));
            var buf = new ArrayBuffer(body.length);
            var view = new Uint8Array(buf);
            for (var i = 0; i < body.length; i++) view[i] = body.charCodeAt(i) & 0xFF;
            return Promise.resolve(buf);
          },
          clone: function() {
            return this;
          }
        };
        resolve(response);
      } catch (e) {
        console.log('[fetch] ERROR: ' + (e && e.message ? e.message : e));
        reject(e);
      }
    });
  };
  // ============================================================
  // Fire DOMContentLoaded and load events after shim is fully loaded
  // ============================================================
  W.setTimeout(function() {
    if (W.dispatchEvent && W.document) {
      console.log("browser_shim.js: dispatch 'DOMContentLoaded' on document");
      W.dispatchEvent(W.document, 'DOMContentLoaded', {});
    }
    if (W.dispatchEvent && W.window) {
      var loadEvent = { type: 'load' };
      console.log("browser_shim.js: dispatch 'load' on window");
      W.dispatchEvent(W.window, 'load', loadEvent);
      if (typeof W.window.onload === 'function') {
        console.log("browser_shim.js: call window.onload",W.window.onload);
        W.window.onload(loadEvent);
      }
    }
  }, 0);

})(typeof window !== 'undefined' ? window : typeof globalThis !== 'undefined' ? globalThis : this);