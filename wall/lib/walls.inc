<?php
/* vim: set syn=php: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

require_once("parapara.inc");
require_once("db.inc");
require_once("exceptions.inc");
require_once("UriUtils.inc");
require_once("designs.inc");
require_once("users.inc");
require_once("utils.inc");
require_once("login.inc");
require_once("characters.inc");

// When updating this, be sure to update login-controller.js as well
define("WALLMAKER_SESSION_NAME", "WMSESSID");

class Wall {
  private $_wallId  = null;
  private $_design  = null;
  private $fields   = null;
  private $sessions = null;
  private $dirtyFields = array();

  static private $hiddenFields   = array('passcode');
  static private $virtualFields  = array('passcodeLen',
                                         'wallUrl', 'editorUrl',
                                         'design', 'thumbnail',
                                         'defaultDuration');
  static private $readOnlyFields = array('wallId', 'editorUrlShort');

  public function __construct($wallId, $fields) {
    $this->_wallId  = $wallId;
    $this->fields   = $fields;
  }

  public function __get($name) {
    // Check if the field is set (i.e. not hidden, not missing needed parameters
    // etc.)
    if (!isset($this->$name)) {
      return null;
    }

    // Regular fields
    if (array_key_exists($name, $this->fields)) {
      return $this->fields[$name];
    }

    // Virtual fields
    switch ($name) {
      case 'wallId':
        return $this->_wallId;

      case 'wallUrl':
        return Wall::getUrlForPath($this->fields['urlPath']);

      case 'editorUrl':
        return Wall::getEditorUrlForPath($this->fields['urlPath']);

      case 'passcodeLen':
        return strlen($this->fields['passcode']);

      case 'design':
        return $this->getDesign();

      case 'thumbnail':
        $design = $this->getDesign();
        return $design ? $design->thumbnail : null;
    }

    return null;
  }

  public function __isset($name) {
    // Hidden fields
    if (in_array($name, self::$hiddenFields))
      return false;

    // Regular fields
    if (array_key_exists($name, $this->fields))
      return true;

    // Virtual fields
    switch ($name) {
      case 'wallId':
        return true;

      case 'wallUrl':
      case 'editorUrl':
        return isset($this->fields['urlPath']);

      case 'passcodeLen':
        return isset($this->fields['passcode']);

      case 'design':
        return $this->getDesign() !== null;

      case 'thumbnail':
        $design = $this->getDesign();
        return $design ? $design->thumbnail !== null : false;
    }

    return false;
  }

  public function __set($name, $value) {
    // Check authorisation
    if (!$this->canAdminister())
      throw new KeyedException('no-auth');

    // Check if we recognize the field
    // (XXX We'll have to change this in future to allow setting hidden fields 
    //  even if you can't *get* them)
    if (!isset($this->$name))
      throw new KeyedException('unknown-field');

    // Reject read-only/virtual fields
    // (XXX Might be better to handle each acceptable field below when we 
    //  sanitize and if it's not recognized, throw there)
    if (in_array($name, self::$virtualFields) ||
        in_array($name, self::$readOnlyFields))
      throw new KeyedException('readonly-field');

    // Sanitize values
    switch($name) {
      case 'name':
        $value = wtrim($value);
        // Don't validate the name if it is identical to the current wall name 
        // (which *ought* to be valid) since otherwise we'll get errors about 
        // the name being a duplicate.
        if ($value !== $this->name) {
          Wall::validateWallName($value);
        }
        break;

      case 'urlPath':
        $value = Wall::sanitizePath($value);
        // As with the name, skip the check if the value is identical to the 
        // current
        if ($value !== $this->urlPath) {
          Wall::validatePath($value);
        }
        break;

      case 'designId':
        $value = intval($value);
        if ($value < 1)
          throw new KeyedException('bad-request');
        DesignGallery::validateDesignId($value);
        break;

      case 'duration':
        // Special handling for "null", " NULL" etc.
        // Since we accept the string "500" it makes sense to extend the same 
        // courtesy to the null value.
        if (is_string($value) && strtolower(trim($value)) === "null")
          $value = null;
        if (!is_numeric($value) && !is_null($value))
          throw new KeyedException('bad-request');
        $value = toIntOrNull($value);
        if ($value < 1 && $value !== null)
          throw new KeyedException('bad-request');
        break;
    }

    // Check if the value is actually different
    // (XXX Likewise we'll need special handling for hidden fields here)
    if ($this->$name === $value)
      return;

    // Regular fields
    if (array_key_exists($name, $this->fields)) {
      $this->fields[$name] = $value;
      // Mark field as dirty
      if (!in_array($name, $this->dirtyFields))
        array_push($this->dirtyFields, $name);
    }

    // Special handling for fields that have calculated values
    switch ($name) {
      case 'urlPath':
        $editorUrlShort = Wall::getShortenedEditorUrlForPath($value);
        if ($editorUrlShort &&
            $editorUrlShort !== @$this->fields['editorShortUrl']) {
          $this->fields['editorUrlShort'] = $editorUrlShort;
          if (!in_array('editorUrlShort', $this->dirtyFields))
            array_push($this->dirtyFields, 'editorUrlShort');
        }
        break;

      case 'designId':
        // Reset cached design
        $this->_design = null;
        break;
    }
  }

  public function save() {
    // Check authorisation
    if (!$this->canAdminister()) {
      throw new KeyedException('no-auth');
    }

    // Check there is something to save
    if (count($this->dirtyFields) === 0)
      return array();

    // Start connection
    $conn =& getDbConnection();

    // Build up pieces of query and result array
    $changedFields = array();
    $assignments = array();
    foreach ($this->dirtyFields as $name) {
      // Determine the field name
      $mappedFields = array('name' => 'eventName',
                            'editorUrlShort' => 'editorShortUrl');
      $fieldName = array_key_exists($name, $mappedFields)
                 ? $mappedFields[$name]
                 : $name;

      // Determine the type of the field
      switch ($fieldName) {
        case 'designId':
        case 'duration':
          $type = 'integer';
          break;

        case 'galleryDisplay':
          $type = 'boolean';
          break;

        case 'createDate':
        case 'modifyDate':
          $type = 'timestamp';
          break;

        default:
          $type = 'text';
          break;
      }
      array_push($assignments,
        $conn->quoteIdentifier($fieldName) . ' = '
        . $conn->quote($this->$name, $type));

      // Assign changed fields (including virtual fields where the dependent 
      // field has changed)
      // (XXX Make sure we don't return hidden fields)
      switch ($name) {
        case 'urlPath':
          $changedFields['wallUrl'] = $this->wallUrl;
          $changedFields['editorUrl'] = $this->editorUrl;
          break;

        case 'designId':
          $changedFields['designId'] = $this->designId;
          $changedFields['thumbnail'] = $this->design->thumbnail;
          $changedFields['defaultDuration'] = $this->design->duration;
          break;

        case 'duration':
          $changedFields['duration'] = $this->duration;
          $changedFields['defaultDuration'] = $this->defaultDuration;

        default:
          $changedFields[$name] = $this->$name;
          break;
      }
    }

    // Update modified date
    array_push($assignments,
      'modifyDate = ' . $conn->quote(gmdate('Y-m-d H:i:s'), 'timestamp'));

    // Run query
    $query = 'UPDATE walls SET '
      . implode(', ', $assignments)
      . ' WHERE wallId = ' . $conn->quote($this->wallId, 'integer');
    $res =& $conn->exec($query);
    checkDbResult($res);
    $conn->disconnect();

    // Reset dirty fields
    $this->dirtyFields = array();

    return $changedFields;
  }

  public function destroy($characterDeleteMode = CharacterDeleteMode::DeleteAll)
  {
    // Check authorization
    if (!$this->canAdminister()) {
      throw new KeyedException('no-auth');
    }

    // Delete characters associated with this wall from DB and from 
    // filesystem depending on the characterDeleteMode parameter
    Characters::deleteByWall($this->wallId, $characterDeleteMode);

    // Setup transaction in case we succeed in deleting sessions but not the 
    // wall
    $conn =& getDbConnection();
    $trans = $conn->beginTransaction();
    checkDbResult($trans);

    // Delete sessions
    $query = 'DELETE FROM sessions'
           . ' WHERE wallId=' . $conn->quote($this->wallId, 'integer');
    $res =& $conn->exec($query);
    checkDbResult($res, $conn);

    // Delete wall
    $query = 'DELETE FROM walls'
           . ' WHERE wallId=' . $conn->quote($this->wallId, 'integer');
    $res =& $conn->exec($query);
    checkDbResult($res, $conn);

    // Commit transaction
    $conn->commit();
    $conn->disconnect();
  }

  public function asArray() {
    // Fields to export if available
    //
    // Associative keys map the regular value name used by users of this class 
    // to another more export-friendly value.
    // (This mapping feature is no longer used and could be removed)
    $keys = array('wallId', 'name',
                  'eventLocation',
                  'eventDescr',
                  'wallUrl', 'editorUrl', 'editorUrlShort',
                  'duration', 'defaultDuration',
                  'passcodeLen',
                  'designId',
                  'thumbnail',
                  'status', 'latestSession');

    $result = array();
    foreach ($keys as $localKey => $keyToExport) {
      $localKey = is_int($localKey) ? $keyToExport : $localKey;
      if (isset($this->$localKey)) {
        $result[$keyToExport] = $this->$localKey;
      }
    }
    return $result;
  }

  public function startSession($datetime = null, $latestSessionId = "Not set") {
    // Sanitize input
    if ($datetime === null)
      $datetime = gmdate("Y-m-d H:i:s");

    // Check authorisation
    if (!$this->canAdminister()) {
      throw new KeyedException('no-auth');
    }

    // Work out the latest session and if it needs closing
    $latestSession = $this->latestSession;

    // Is the passed-in session ID correct?
    if ($latestSessionId != "Not set" &&
        toIntOrNull($latestSession['sessionId']) !== $latestSessionId) {
      return false;
    }

    // Close the latest session if it's open
    if (@$latestSession['end'] === null) {
      $this->endSession($datetime, $latestSession['sessionId']);
    }

    // Get sessionId for next session
    $conn =& getDbConnection();
    $quotedWallId = $conn->quote($this->wallId, 'integer');
    $newSessionId =& $conn->queryOne(
        'SELECT IFNULL(MAX(sessionId)+1, 1)'
        . ' FROM sessions WHERE wallId = ' . $quotedWallId
        . ' LIMIT 1',
        'integer');
    checkDbResult($newSessionId);

    // Start a new session
    $query = 'INSERT INTO sessions(wallId, sessionId, beginDate)'
      . ' VALUES(' . $quotedWallId
      . ',' . $conn->quote($newSessionId, 'integer')
      . ',' . $conn->quote($datetime, 'timestamp')
      . ')';
    $res =& $conn->exec($query);
    checkDbResult($res);

    // Update latest session
    $this->fields['latestSession']['sessionId'] = $newSessionId;
    $this->fields['latestSession']['start']     = $datetime;
    $this->fields['latestSession']['end']       = null;
    $this->fields['status'] = 'running';

    $conn->disconnect();

    return true;
  }

  public function endSession($datetime = null, $latestSessionId = "Not set") {
    // Sanitize input
    if ($datetime === null)
      $datetime = gmdate("Y-m-d H:i:s");

    // Check authorisation
    if (!$this->canAdminister()) {
      throw new KeyedException('no-auth');
    }

    // Check that:
    // (a) there is actually a session to close
    // (b) the session is not already closed
    $latestSession = $this->latestSession;
    if ($latestSession === null || $latestSession['end'] !== null)
      return false;

    // Check the passed-in session ID, if any, matches the latest session
    if ($latestSessionId != "Not set" &&
        $latestSession['sessionId'] !== $latestSessionId)
      return false;
    $latestSessionId = $latestSession['sessionId'];

    // End session
    $conn =& getDbConnection();
    $query = 'UPDATE sessions'
      . ' SET endDate=' . $conn->quote($datetime, 'timestamp')
      . ' WHERE sessionId=' . $conn->quote($latestSessionId, 'integer')
      . ' AND wallId=' . $conn->quote($this->wallId, 'integer');
    $res =& $conn->exec($query);
    checkDbResult($res);
    $conn->disconnect();

    // Update latest session
    $this->fields['latestSession']['end'] = $datetime;
    $this->fields['status'] = 'finished';

    // Return true since we made a change
    return true;
  }

  public function restartSession($latestSessionId = "Not set") {
    // Check authorisation
    if (!$this->canAdminister()) {
      throw new KeyedException('no-auth');
    }

    // Check that:
    // (a) there is actually a session to close
    // (b) the session is not already open
    $latestSession = $this->latestSession;
    if ($latestSession === null || $latestSession['end'] === null)
      return false;

    // Check the passed-in session ID, if any, matches the latest session
    if ($latestSessionId != "Not set" &&
        $latestSession['sessionId'] !== $latestSessionId)
      return false;
    $latestSessionId = $latestSession['sessionId'];

    // Re-open session
    $conn =& getDbConnection();
    $query = 'UPDATE sessions'
      . ' SET endDate=NULL'
      . ' WHERE sessionId=' . $conn->quote($latestSessionId, 'integer')
      . ' AND wallId=' . $conn->quote($this->wallId, 'integer');
    $res =& $conn->exec($query);
    checkDbResult($res);
    $conn->disconnect();

    // Update latest session
    $this->fields['latestSession']['end'] = null;
    $this->fields['status'] = 'running';

    return true;
  }

  public function getSessions($includeCharacters = null) {
    // Query sessions
    $conn =& getDbConnection();
    $res =& $conn->query(
      'SELECT sessionId, beginDate as start, endDate as end FROM sessions'
      . ' WHERE wallId = ' . $conn->quote($this->wallId, 'integer')
      . ' ORDER BY sessionId');
    checkDbResult($res);

    // Process results
    $result = array();
    $conn->setFetchMode(MDB2_FETCHMODE_ASSOC);
    while ($row = $res->fetchRow()) {
      array_push($result, 
        array('sessionId' => intval($row['sessionid']),
              'start' => $row['start'],
              'end' => $row['end']));
    }
    $conn->disconnect();

    // Insert characters if required
    if ($includeCharacters == "Include characters") {
      $characters = Characters::getByWall($this->wallId);
      foreach ($result as &$session) {
        $sessionId = $session['sessionId'];
        if (array_key_exists($sessionId, $characters)) {
          $session['characters'] = $characters[$sessionId];
        } else {
          $session['characters'] = array();
        }
      }
    }

    return $result;
  }

  public function deleteSession($sessionId,
    $characterDeleteMode = CharacterDeleteMode::DeleteAll)
  {
    // Check authorization
    if (!$this->canAdminister())
      throw new KeyedException('no-auth');

    // Sanity checking
    $sessionId = toIntOrNull($sessionId);
    if (!$sessionId || $sessionId < 1)
      throw new KeyedException('bad-request', "Bad session ID");

    // Check it is a valid session ID for this wall
    $conn =& getDbConnection();
    $row =& $conn->queryRow(
        "SELECT sessionId FROM sessions "
        . ' WHERE sessionId=' . $conn->quote($sessionId, 'integer')
        . ' AND wallId=' . $conn->quote($this->wallId, 'integer')
        . " LIMIT 1");
    checkDbResult($row);
    if (!$row)
      throw new KeyedException('session-not-found');

    // Check if the session we are about to delete is the latest session.
    // If it is the latest then we will have to have to update some state when 
    // done.
    $isLatest = $this->latestSession &&
                $this->latestSession['sessionId'] == $sessionId;


    // Delete characters associated with this session from DB and from 
    // filesystem depending on the characterDeleteMode parameter
    Characters::deleteBySession($this->wallId, $sessionId,
                                $characterDeleteMode);

    // Delete session
    $query = 'DELETE FROM sessions'
      . ' WHERE sessionId=' . $conn->quote($sessionId, 'integer')
      . ' AND wallId=' . $conn->quote($this->wallId, 'integer');
    $res =& $conn->exec($query);
    checkDbResult($res);
    $conn->disconnect();

    // Update latest session state
    if ($isLatest) {
      $this->updateLatestSession();
    }

    return true;
  }

  /* 
   * Updates the latestSession member.
   *
   * Typically this is not needed since any changes made to this object are 
   * careful to update this state automatically.
   *
   * However, for long running processes (such as the live feed) when other 
   * processes may updating the database simultaneously this offers a cheap way 
   * to update the session information without refetching the object.
   */
  public function updateLatestSession() {
    // Fetch the latest session
    $conn =& getDbConnection();
    $row =& $conn->queryRow(
      'SELECT sessionId, beginDate as start, endDate as end FROM sessions'
      . ' WHERE wallId = ' . $conn->quote($this->wallId, 'integer')
      . ' ORDER BY sessionId DESC LIMIT 1', null, MDB2_FETCHMODE_ASSOC);
    checkDbResult($row);
    $conn->disconnect();

    // Update the latest session parameter
    if ($row) {
      $session['sessionId'] = intval($row['sessionid']);
      $session['start'] = $row['start'];
      $session['end'] = $row['end'];
      $this->fields['latestSession'] = $session;
    } else {
      $this->fields['latestSession'] = null;
    }

    // Update the status while we're at it
    $this->fields['status'] =
      $this->latestSession && !$this->latestSession['end']
      ? 'running' : 'finished';
  }

  /* Returns a real number in the range [0,1) indicating the fractional progress
   * of the wall through its cycle. */
  public function getCurrentProgress() {
    $effectiveDuration = $this->duration
                       ? $this->duration
                       : $this->defaultDuration;
    $currentTimeMillis = bcmul(microtime(true), 1000);
    $absoluteTime = bcmod($currentTimeMillis, $effectiveDuration);
    return $absoluteTime / $effectiveDuration;
  }

  public static function getPathForName($name) {
    // Sanitize
    $path = Wall::sanitizePath($name);

    // Check for duplicates
    // If we have a clash, just generate a random ID and use that--the user can 
    // make it something more sensible later if they wish
    if (!Walls::isPathUnique($path))
      $path = rawurlencode(uniqid());

    return $path;
  }

  public static function getUrlForPath($path) {
    return getCurrentServer() . '/walls/' . rawurlencode($path);
  }

  public static function getEditorUrlForPath($path) {
    global $config;
    return $config['editor']['url'] . rawurlencode($path);
  }

  // Returns the shortened editor URL for a given path or NULL if no shortening 
  // service is available
  public static function getShortenedEditorUrlForPath($path) {
    $editorUrl = Wall::getEditorUrlForPath($path);

    $editorUrlShort = shortenUrl($editorUrl);
    return $editorUrlShort == $editorUrl ? NULL : $editorUrlShort;
  }

  public static function validateWallName($name) {
    // Check name has been trimmed
    if ($name !== wtrim($name))
      throw new KeyedException('bad-name');

    // Check name is not empty
    if (!strlen($name))
      throw new KeyedException('empty-name');

    // Check name is unique
    if (!Walls::isNameUnique($name)) {
      error_log("Wall with name '$name' already exists.");
      throw new KeyedException('duplicate-name');
    }
  }

  public static function sanitizePath($path) {
    $path = mb_convert_kana(wtrim($path), "as", "utf-8");

    // Make lowercase, and
    // convert whitespace and URL-like characters to -
    $path = strtr(strtolower($path), " \t/:.?=&+", '---------');

    return $path;
  }

  public static function validatePath($path) {
    // Check the path has been sanitized
    if ($path !== self::sanitizePath($path))
      throw new KeyedException('bad-path');

    // Check the path exists
    if (!strlen($path))
      throw new KeyedException('bad-path');

    // Check it is unique
    if (!Walls::isPathUnique($path)) {
      error_log("Wall with path '$path' already exists.");
      throw new KeyedException('duplicate-path');
    }
  }

  public function canAdminister() {
    // In the future we will check if the wall has been shared with the current 
    // user or not
    return $this->isOwner();
  }

  public function isOwner() {
    $userEmail = getUserEmailSilently();
    if (!$userEmail || $this->ownerEmail === null)
      return false;
    return $userEmail == $this->ownerEmail;
  }

  private function getDesign() {
    if ($this->_design === null && $this->designId) {
      $this->_design = DesignGallery::getById($this->designId);
    }
    return $this->_design;
  }
}

class Walls {
  public static function create($name, $designId) {
    // Get connection
    $conn =& getDbConnection();

    // Validate owner email
    $email = getUserEmailSilently();
    if (!strlen($email) || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
      error_log("Bad or missing email when creating wall");
      throw new KeyedException('bad-email', 'Bad email for user');
    }

    // Fetch user ID for owner
    $ownerId = Users::getOrCreateFromEmail($email);

    // Validate wall name
    $name = wtrim($name);
    Wall::validateWallName($name);

    // Validate design ID
    $design = DesignGallery::getById($designId);
    if (!$design) {
      error_log("Could't find design ID " . $designId);
      throw new KeyedException('design-not-found', 'ID not found');
    }

    // Get URLs
    $path      = Wall::getPathForName($name);
    $wallUrl   = Wall::getUrlForPath($path);
    $editorUrl = Wall::getEditorUrlForPath($path);

    // Create shortened versions
    $editorShortUrl = Wall::getShortenedEditorUrlForPath($path);

    // Store in database
    $date = gmdate('Y-m-d H:i:s');
    $query =
      'INSERT INTO walls'
      . ' (owner, designId, eventName, galleryDisplay, urlPath,'
      . '  editorShortUrl, createDate, modifyDate)'
      . ' VALUES '
      . '(' . $conn->quote($ownerId, 'integer')
      . ',' . $conn->quote($designId, 'integer')
      . ',' . $conn->quote($name, 'text')
      . ',FALSE' // galleryDisplay
      . ',' . $conn->quote($path, 'text')
      . ',' . (($editorShortUrl) ? $conn->quote($editorShortUrl, 'text')
                                 : 'NULL')
      . ',' . $conn->quote($date, 'timestamp')
      . ',' . $conn->quote($date, 'timestamp')
      . ')';
    $res =& $conn->query($query);
    checkDbResult($res);

    // Get ID of newly created wall
    $wallId = $conn->lastInsertID('walls', 'wallId');
    checkDbResult($wallId);

    // Return newly created wall object
    // (We could just create the Wall object from the information we have on 
    // hand but it's just simpler to re-use the getById look up code and saves 
    // us from bugs where we set things in one place and not the other)
    return self::getById($wallId);
  }

  public static function getById($wallId) {
    return self::getWall($wallId, null);
  }

  public static function getByPath($wallPath) {
    return self::getWall(null, $wallPath);
  }

  protected static function getWall($wallId, $wallPath) {
    // Prepare query
    $conn =& getDbConnection();
    $query =
        'SELECT walls.wallId, walls.designId'
      . ' ,eventName, eventDescr, eventLocation, eventType, eventFinish'
      . ' ,urlPath, editorShortUrl'
      . ' ,walls.duration, galleryDisplay, passcode'
      . ' ,designs.duration AS defaultDuration'
      . ' ,users.email AS ownerEmail'
      . ' ,sessionId, beginDate as sessionStart, endDate as sessionEnd'
      . ' FROM walls'
      . ' INNER JOIN designs ON walls.designId = designs.designId'
      . ' LEFT JOIN users ON'
      . '   walls.owner = users.userId'
      . ' LEFT JOIN sessions ON'
      . '   sessions.sessionSerial = '
      . '   (SELECT MAX(sessionSerial) FROM sessions'
      . '    WHERE sessions.wallId = walls.wallId)'
      . ' WHERE ';
    if ($wallId) {
      $query .= 'walls.wallId = ' . $conn->quote($wallId, 'integer');
    } else {
      $query .= 'walls.urlPath = ' . $conn->quote($wallPath, 'text');
    }
    $query .= ' LIMIT 1';

    // Run query
    $row =& $conn->queryRow($query, null, MDB2_FETCHMODE_ASSOC);
    $conn->disconnect();
    checkDbResult($row);

    // Check if wall was found
    if ($row === null)
      return null;

    return self::dbRowToWall($row);
  }

  public static function getAllForUser($email) {
    // Prepare query
    $conn =& getDbConnection();
    $query =
        'SELECT walls.wallId, walls.designId'
      . ' ,eventName, eventDescr, eventLocation, eventType, eventFinish'
      . ' ,urlPath, editorShortUrl'
      . ' ,walls.duration, galleryDisplay, passcode'
      . ' ,designs.duration AS defaultDuration'
      . ' ,users.email AS ownerEmail'
      . ' ,sessionId, beginDate as sessionStart, endDate as sessionEnd'
      . ' FROM walls'
      . ' INNER JOIN designs ON walls.designId = designs.designId'
      . ' INNER JOIN users ON'
      . '   walls.owner = users.userId AND'
      . '   users.email = ' . $conn->quote($email)
      . ' LEFT JOIN sessions ON'
      . '   sessions.sessionSerial = '
      . '   (SELECT MAX(sessionSerial) FROM sessions'
      . '    WHERE sessions.wallId = walls.wallId)'
      . ' ORDER BY walls.createDate DESC, walls.wallId DESC';
      // XXX It's probably better to just sort this in ascending order by wallId
      // and when we *display* it, reverse the order as needed

    // Run query
    $res =& $conn->query($query);
    checkDbResult($res);

    // Prepare result
    $result = array();
    $conn->setFetchMode(MDB2_FETCHMODE_ASSOC);
    while ($row = $res->fetchRow()) {
      array_push($result, self::dbRowToWall($row));
    }
    $conn->disconnect();

    return $result;
  }

  public static function getAllPublic() {
    // Prepare query
    $conn =& getDbConnection();
    $query =
        'SELECT walls.wallId, walls.designId ,eventName, urlPath'
      . ' ,sessionId, beginDate as sessionStart, endDate as sessionEnd'
      . ' FROM walls'
      . ' LEFT JOIN sessions ON'
      . '   sessions.sessionSerial = '
      . '   (SELECT MAX(sessionSerial) FROM sessions'
      . '    WHERE sessions.wallId = walls.wallId)'
      . ' WHERE galleryDisplay = TRUE'
      . ' ORDER BY walls.wallId ASC';

    // Run query
    $res =& $conn->query($query);
    checkDbResult($res);

    // Prepare result
    $result = array();
    $conn->setFetchMode(MDB2_FETCHMODE_ASSOC);
    while ($row = $res->fetchRow()) {
      array_push($result, self::dbRowToWall($row));
    }
    $conn->disconnect();

    return $result;
  }

  private static function dbRowToWall($row) {
    // Get wall ID -- this is the only required field
    if (!isset($row['wallid'])) {
      throw new KeyedException('server-error');
    }
    $wallId = intval($row['wallid']);

    // Basic information
    self::copyFieldIfSet($fields, $row, "name", "eventname");
    self::copyFieldIfSet($fields, $row, "designId", null, 'toIntOrNull');
    self::copyFieldIfSet($fields, $row, "editorUrlShort", "editorshorturl");
    foreach(array("urlPath", "ownerEmail") as $key) {
      self::copyFieldIfSet($fields, $row, $key);
    }

    // Session information
    if (@$row['sessionid']) {
      // Got a session
      self::copyFieldIfSet($session, $row, "sessionId", null, 'toIntOrNull');
      self::copyFieldIfSet($session, $row, "start", "sessionstart");
      self::copyFieldIfSet($session, $row, "end", "sessionend");
      $fields['latestSession'] = $session;
      $fields['status'] = @$row['sessionend'] ? 'finished' : 'running';
    } else if (array_key_exists('sessionid', $row)) {
      // Got no sessions *but* we looked for one
      $fields['latestSession'] = null;
      $fields['status'] = 'finished';
    }

    // Duration information
    foreach(array("duration", "defaultDuration") as $key) {
      self::copyFieldIfSet($fields, $row, $key, null, 'toIntOrNull');
    }

    return new Wall($wallId, $fields);
  }

  protected static function copyFieldIfSet(&$fields, &$row, $key,
                                           $rowKey = null,
                                           $sanitizeFunc = null) {
    if ($rowKey === null) {
      $rowKey = strtolower($key);
    }
    if (array_key_exists($rowKey, $row)) {
      if ($sanitizeFunc === null) {
        $fields[$key] = $row[$rowKey];
      } else {
        $fields[$key] = $sanitizeFunc($row[$rowKey]);
      }
    }
  }

  public static function isNameUnique($name) {
    return !self::recordExists('walls', 'eventName', $name, 'text');
  }

  public static function isPathUnique($path) {
    return !self::recordExists('walls', 'urlPath', $path, 'text');
  }

  private static function recordExists($table, $field, $value, $type) {
    $conn =& getDbConnection();
    $res =& $conn->query(
        "SELECT * FROM $table WHERE BINARY $field = "
        . $conn->quote($value, $type)
        . " LIMIT 1");
    checkDbResult($res);
    $exists = $res->numRows() > 0;
    $res->free();
    return $exists;
  }
}

?>
